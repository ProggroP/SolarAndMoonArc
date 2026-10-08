#include <pebble.h>
#include "gradient.h"

/* ===========================================================================
 * solararc.c - Sonnen-/Mond-Bahn-Watchface
 * ---------------------------------------------------------------------------
 * Sonne und Mond stehen auf einer festen Bahn. Ihre HOEHE auf dem Bildschirm
 * entspricht dem ECHTEN Hoehenwinkel ueber dem Horizont (0 Grad am Horizont,
 * 90 Grad obere Bildkante - 90 Grad nur am Aequator erreichbar).
 *
 * Das Telefon (PebbleKit JS) liefert die langsam veraenderlichen Groessen:
 *   Breite, Sonnen-/Mond-Deklination, Stundenwinkel je Koerper zum Sendezeit-
 *   punkt, plus die Daemmerungs-Schwellen fuer die Himmelsfarbe.
 * Die Uhr rechnet aus Auf-/Untergangszeiten den Bahnfortschritt und daraus
 * den Stundenwinkel (Rate pro Minute) und die echte Hoehe.
 *
 * Sonne: Bahnposition aus Sonnenauf-/untergang (lokale Minuten).
 * Mond:  Bahnposition aus Mondauf-/untergang (Telefon), wie Sonne.
 * Beide: Horizont = GROUND_PCT (baseline), nicht unterer Bildrand.
 * Hoehe: echter Hoehenwinkel (0 Grad = Horizont, 90 Grad = oberer Himmelsrand).
 *        Am Aequator kann 90 Grad erreicht werden, bei mittleren Breiten ~60 Grad.
 *
 * Himmel: vertikaler, geditherter Verlauf (gradient.h), Farbe nach Phase.
 * Horizont: schwarze Silhouette (Huegel, Baum, Windrad, Haus), programmatisch.
 * ========================================================================= */

#define R  TRIG_MAX_RATIO

/* ---- LAYOUT: modellabhaengig ------------------------------------------- */
/* SUN/MOON_ARC_EDGE_PX: Abstand Auf-/Untergang zur linken/rechten Kante (px) */
/* SIDE_ELEM_GAP_PX:    Abstand Baum/Haus vom zentralen Windrad (px, symmetrisch) */
/* SIDE_ELEM_JITTER_PX: max. Zufallsversatz je Seite (+/-), einmal beim Start gesetzt */
#if defined(PBL_PLATFORM_EMERY)
  #define SUN_ARC_EDGE_PX    6
  #define MOON_ARC_EDGE_PX  34
  #define SIDE_ELEM_GAP_PX  64
  #define SIDE_ELEM_JITTER_PX 10   /* NEU */
#elif defined(PBL_PLATFORM_GABBRO)
  #define SUN_ARC_EDGE_PX   30
  #define MOON_ARC_EDGE_PX  44
  #define SIDE_ELEM_GAP_PX  54
  #define SIDE_ELEM_JITTER_PX 10   /* NEU */
#elif defined(PBL_PLATFORM_CHALK)
  #define SUN_ARC_EDGE_PX   25
  #define MOON_ARC_EDGE_PX  24
  #define SIDE_ELEM_GAP_PX  36
  #define SIDE_ELEM_JITTER_PX  8   /* NEU */
#else  /* aplite, basalt, diorite, flint – 144×168 */
  #define SUN_ARC_EDGE_PX    4
  #define MOON_ARC_EDGE_PX  24
  #define SIDE_ELEM_GAP_PX  46
  #define SIDE_ELEM_JITTER_PX  6   /* NEU */
#endif

/* ---- LAYOUT (gemeinsam, Prozent) --------------------------------------- */
#define GROUND_PCT           5   /* Bodenhoehe von unten                    */
#define ARC_APEX_TOP_PCT     6   /* Y bei 90 Grad Hoehenwinkel (Himmelskante oben) */
#define SUN_R_PCT            8
#define MOON_R_PCT           6
#define HILL_AMP_PCT         3
#define HILL_BUMPS           2

#define TREE_FOLIAGE_CY_PCT 13   /* Kronenmitte ueber Boden                 */
#define TREE_R_PCT           6
#define TURB_HUB_PCT        16    /* Nabenhoehe ueber Boden                  */
#define TURB_BLADE_PCT      11
#define HOUSE_W_PCT         15
#define HOUSE_BODY_PCT      12
#define HOUSE_ROOF_PCT       7
/* Tuer und Fenster als Anteil der Hauskoerper-Masse (Prozent) */
#define HOUSE_DOOR_W_PCT    35   /* Tuerbreite  als % von hw  */
#define HOUSE_DOOR_H_PCT    65   /* Tuerhoehe   als % von bh  */
#define HOUSE_WIN_W_PCT     50   /* Fensterseite als % von hw (quadratisch) */
#define HOUSE_WIN_MARGIN_PCT 4   /* Abstand Fenster-Oberkante von body_top als % von bh */

/* Hausfarben – alle im 64-Farben-Raster (0/85/170/255), da keine Dithering */
#ifdef PBL_COLOR
  #define HOUSE_FRAME_COLOR    GColorLightGray   /* Tuer- und Fensterrahmen  */
  #define HOUSE_WIN_DAY_COLOR  GColorChromeYellow /* Fenster tagsüber: hell   */
  #define HOUSE_WIN_NIGHT_COLOR GColorWindsorTan  /* Fenster nachts: warm     */
#else
  #define HOUSE_FRAME_COLOR    GColorWhite
  #define HOUSE_WIN_DAY_COLOR  GColorWhite
  #define HOUSE_WIN_NIGHT_COLOR GColorWhite
#endif

/* ---- FONTS ------------------------------------------------------------- */
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  #define FONT_TIME    FONT_KEY_LECO_60_NUMBERS_AM_PM
  #define TIME_BOX_H   70
  #define FONT_DATE    FONT_KEY_GOTHIC_28_BOLD
  #define DATE_BOX_H   28
#else
  #define FONT_TIME    FONT_KEY_LECO_42_NUMBERS
  #define TIME_BOX_H   50
  #define FONT_DATE    FONT_KEY_GOTHIC_24_BOLD
  #define DATE_BOX_H   22
#endif
#define DATE_GAP_PX    4    /* Abstand Datum -> Uhrzeit (Altwert, ungenutzt)  */

/* ---- TEXT-POSITIONEN je Modell (NEU) ----------------------------------- */
/* Oberkante der drei Textboxen in Pixeln, pro Modell frei einstellbar.      */
/*   TIME_TOP_Y     Zeitfeld oben    (wenn "Show time at top" aktiv)         */
/*   DATE_Y         Datumsfeld       (feste Stelle, wenn Datum aktiv)        */
/*   TIME_BOTTOM_Y  Zeitfeld unten   (Standardposition wie Screenshot)       */
#if defined(PBL_PLATFORM_EMERY)
  #define TIME_TOP_Y     0
  #define DATE_Y         59
  #define TIME_BOTTOM_Y  79
#elif defined(PBL_PLATFORM_GABBRO)
  #define TIME_TOP_Y     20
  #define DATE_Y         79
  #define TIME_BOTTOM_Y 101
#elif defined(PBL_PLATFORM_CHALK)
  #define TIME_TOP_Y     14
  #define DATE_Y         54
  #define TIME_BOTTOM_Y  74
#else  /* aplite, basalt, diorite, flint – 144×168 */
  #define TIME_TOP_Y     3
  #define DATE_Y         42
  #define TIME_BOTTOM_Y  60
#endif

/* ---- STUNDENSTREIFEN je Modell (Wettermodus 3) ------------------------- */
/*   STRIP_TOP_CY  Wolkenmitte oben, ueber dem Datum (Zeit an Standardposition) */
/*   STRIP_MID_CY  Wolkenmitte zwischen Datum und Silhouette ("Time at top")    */
/*   STRIP_DX      Abstand der Nachbarstunden von der Mitte                     */
/* Rund: oben ist die Sehne schmal, daher dort tiefer und enger.             */
#if defined(PBL_PLATFORM_EMERY)
  #define STRIP_TOP_CY   27
  #define STRIP_MID_CY  135
  #define STRIP_DX       52
#elif defined(PBL_PLATFORM_GABBRO)
  #define STRIP_TOP_CY   40
  #define STRIP_MID_CY  142
  #define STRIP_DX       50
#elif defined(PBL_PLATFORM_CHALK)
  #define STRIP_TOP_CY   31
  #define STRIP_MID_CY   98
  #define STRIP_DX       40
#else  /* aplite, basalt, diorite, flint – 144×168 */
  #define STRIP_TOP_CY   20
  #define STRIP_MID_CY   90
  #define STRIP_DX       40
#endif

/* ---- BEWEGUNGSRATEN der Stundenwinkel ---------------------------------- */
/* Sonne: 0,25 Grad/min. Mond: ~0,2415 Grad/min (laengerer Mondtag).        */
#define SUN_RATE_UDEG_MIN  250000L
#define MOON_RATE_UDEG_MIN 241530L

/* ---- REFRESH ----------------------------------------------------------- */
/* Die Uhr sendet REQUEST_UPDATE nur beim Tageswechsel (00:00) und einmal    */
/* beim ersten Start ohne Daten. Das Telefon entscheidet ob es tatsaechlich  */
/* rechnet (es kennt seinen letzten Sendezeitpunkt via localStorage).        */
/* Settings (Toggles) werden vom Telefon bei jedem Clay-Close sofort und     */
/* separat gesendet, ohne GPS-Abfrage.                                       */
#define AHEAD_OF_TIME_MIN  30    /* Bahn-Vorlauf in Minuten (Toggle an)      */

/* ---- WINDRAD-ANIMATION (NEU) ------------------------------------------- */
/* Bei aktivem Toggle dreht das Windrad bei jedem Minutenwechsel und bleibt  */
/* TURBINE_STEP_DEG weiter als zuvor stehen. Die zusaetzlichen Umdrehungen   */
/* machen die Drehung sichtbar; die Gesamtdauer bleibt <= TURBINE_ANIM_MS.   */
/* Bei AUSGESCHALTETER Animation springt das Windrad jede Minute hart um     */
/* TURBINE_STEP_DEG weiter (ohne Zwischenframes).                            */
#define TURBINE_STEP_DEG      21    /* Weiterdrehung je Minute (Grad)         */
#define TURBINE_ANIM_TURNS     2    /* zusaetzliche volle Umdrehungen         */
#define TURBINE_ANIM_MS     2000    /* max. Dauer der Drehung (ms)            */
#define TURBINE_ANIM_FRAME_MS 50    /* Frame-Intervall (~20 FPS)              */

/* ---- DEBUG: Zeitraffer (zum Testen von Gradient/Bahn) ------------------ */
/* Auskommentieren zum Aktivieren – jede Sekunde +DEBUG_TIMELAPSE_MIN_PER_SEC */
// #define DEBUG_TIMELAPSE
#ifdef DEBUG_TIMELAPSE
  #define DEBUG_TIMELAPSE_MIN_PER_SEC  10
#endif

/* ---- WETTER (Open-Meteo, vom Telefon) ------------------------------------
 * Das Telefon liefert 48 Stundenwerte ab WX_START (heute + morgen). Der
 * aktuelle Zustand ist einfach die laufende Stunde - so bleibt das Wetter
 * auch ohne Telefon bis zu einem Tag lang stimmig. */
#define WX_MODE_OFF            0
#define WX_MODE_SKY            1    /* nur Himmelsfarbe + Windrad             */
#define WX_MODE_CLOUDS         2    /* zusaetzlich Wolken auf der Sonnenbahn  */
#define WX_MODE_STRIP          3    /* zusaetzlich Vorstunde / jetzt / naechste Stunde */
#define WX_HOURS              48
#define WX_DATA_VERSION        1
#define WX_TEMP_OFFSET       100    /* Temperatur + 100 im Byte, 0 = unbekannt */
#define WX_REFRESH_S        3600    /* Wetter so alt -> neu anfordern          */
#define WX_RETRY_S           600    /* fruehestens so oft erneut anfragen      */
#define WX_TINT_FROM_PCT      20    /* Bedeckung, ab der der Himmel vergraut   */
#define WX_TINT_MAX          170    /* Grauanteil bei 100 % (von 256)          */
#define WX_PRECIP_TINT_EXTRA  40    /* zusaetzlich bei Regen/Schnee/Gewitter   */
#define WX_OVERCAST_DIM_PCT   85    /* Grauton etwas dunkler als die Helligkeit */
#define WX_FOG_MIX           150    /* Nebelanteil am Horizont (von 256)       */
#define WX_CLOUD_MIN_PCT      50    /* ab hier Wolke auf der Sonnenbahn        */
#define WX_CLOUD_BIG_PCT      80    /* ab hier grosse Wolke                    */
#define WX_CLOUD_SMALL_U_PCT  70    /* Wolkengroesse in % des Sonnenradius     */
#define WX_CLOUD_BIG_U_PCT   100
#define WX_SLOT_MIN_MIN       15    /* Stundenrest am Auf-/Untergang darunter: keine Wolke */
#define WX_STRIP_FROM_PCT     20    /* Streifen: darunter bleibt der Platz leer  */
#define WX_STRIP_NOW_U_PCT   100    /* aktuelle Stunde, % des Sonnenradius       */
#define WX_STRIP_SIDE_U_PCT   70    /* Vor- und naechste Stunde                  */
#define WX_STRIP_THIN_PCT     70    /* unter WX_CLOUD_MIN_PCT: Wolke so viel kleiner */
#define WX_MAX_CLOUDS         24
#define WX_CALM_KMH            3    /* darunter steht das Windrad still        */
#define WX_TURB_DEG_PER_KMH    2    /* Drehung je Minute pro km/h              */
#define WX_TURB_MAX_STEP_DEG 120
#define WX_KMH_PER_TURN       12    /* Animation: eine Extra-Umdrehung je 12 km/h */
#define WX_TURB_MAX_TURNS      4

/* Niederschlagsart, Bit 4-6 des Stundenbytes (Bit 0-3: Bedeckung in Zehnteln) */
enum { WX_P_NONE = 0, WX_P_RAIN, WX_P_SNOW, WX_P_THUNDER, WX_P_FOG };

/* ---- Wolkenfarben ------------------------------------------------------- */
#define CLOUD_DAY_FILL        GColorWhite
#define CLOUD_DAY_SHADE       GColorLightGray
#define CLOUD_DAY_EDGE        GColorDarkGray
#define CLOUD_DAY_WET_FILL    GColorLightGray     /* Regenwolke: dunkler   */
#define CLOUD_DAY_WET_SHADE   GColorDarkGray
#define CLOUD_DAY_WET_EDGE    GColorDarkGray
/* Nachts zurueckhaltend: ohne hellen Rand, Unterseite verlaeuft in den
 * Nachthimmel. Ein heller Rand machte die Vorhersage zur Wolkenmauer. */
#define CLOUD_NIGHT_FILL      GColorDarkGray
#define CLOUD_NIGHT_SHADE     GColorOxfordBlue
#define CLOUD_NIGHT_EDGE      GColorOxfordBlue
#define CLOUD_NIGHT_WET_FILL  GColorDarkGray
#define CLOUD_NIGHT_WET_SHADE GColorBlack
#define CLOUD_NIGHT_WET_EDGE  GColorBlack
#define RAIN_DAY_COLOR        GColorCobaltBlue
#define RAIN_NIGHT_COLOR      GColorPictonBlue
#define SNOW_DAY_COLOR        GColorWhite
#define SNOW_NIGHT_COLOR      GColorWhite
#define BOLT_COLOR            GColorYellow

#define SKY_NIGHT_FADE_MIN  45   /* Uebergang Nacht <-> Astro nach Daemmerungsende */
#define RISE_ZENITH_MDEG  90833  /* NOAA: 90.833 Grad (Sonne, Telefon)            */

/* ---- FARBEN: Himmel-Stuetzstellen (top, horizon), 8-Bit, werden gedithert */
#define NIGHT_TOP  GRAD_RGB( 12, 12, 60)
#define NIGHT_HOR  GRAD_RGB(  0,  0,  0)
#define ASTRO_TOP  GRAD_RGB(  0,  0,110)
#define ASTRO_HOR  GRAD_RGB(  4,  4, 30)
#define NAUT_TOP   GRAD_RGB( 12, 30,130)
#define NAUT_HOR   GRAD_RGB( 10, 12, 55)
#define CIVIL_TOP  GRAD_RGB( 45, 95,180)
#define CIVIL_HOR  GRAD_RGB(110, 80,120)
#define GOLD_TOP   GRAD_RGB( 95,150,210)
#define GOLD_HOR   GRAD_RGB(255,150, 45)
#define DAY_TOP    GRAD_RGB( 95,185,235)
#define DAY_HOR    GRAD_RGB(200,232,255)

/* ---- FARBEN: Saettigungsboost (MORE_SAT) -------------------------------- */
/* Nacht/Astro identisch. Ab nautisch bis Tag: kraeftigere, reinere Toene.   */
#define SAT_NAUT_TOP   GRAD_RGB(  0, 15,170)
#define SAT_NAUT_HOR   GRAD_RGB(  0, 10, 90)
#define SAT_CIVIL_TOP  GRAD_RGB(  0, 60,220)
#define SAT_CIVIL_HOR  GRAD_RGB(170, 50,160)
#define SAT_GOLD_TOP   GRAD_RGB( 30,100,220)
#define SAT_GOLD_HOR   GRAD_RGB(255,120,  0)
#define SAT_DAY_TOP    GRAD_RGB( 30,170,255)
#define SAT_DAY_HOR    GRAD_RGB(100,210,255)

/* Text Auto-Cycle: Fuell- und Schattenfarbe je Himmels-Phase (8-Bit RGB)     */
#define TF_NIGHT   GRAD_RGB(255,255,255)   /* weiss             */
#define TS_NIGHT   GRAD_RGB(  0,  0,  0)
#define TF_ASTRO   GRAD_RGB(170,255,255)   /* Celeste (kuehl)   */
#define TS_ASTRO   GRAD_RGB(  0,  0,  0)
#define TF_NAUT    GRAD_RGB(170,170,255)   /* Babyblau          */
#define TS_NAUT    GRAD_RGB(  0,  0,  0)
#define TF_CIVIL   GRAD_RGB(255,170,170)   /* Lachs (warm)      */
#define TS_CIVIL   GRAD_RGB(  0,  0,  0)
#define TF_GOLD    GRAD_RGB(255,255, 85)   /* Gelb              */
#define TS_GOLD    GRAD_RGB(  0,  0,  0)
#define TF_DAY     GRAD_RGB(255,255,255)   /* weiss             */
#define TS_DAY     GRAD_RGB(  0,  0,  0)

/* ---- HIGH CONTRAST (NEU) ----------------------------------------------- */
/* Eigener, gut lesbarer Modus mit hartem Wechsel zwischen zwei Paaren:      */
/* heller Himmel (golden_rise..golden_set) -> HC_DAY_*, sonst HC_NIGHT_*.    */
/* Kein weicher Uebergang, damit der Kontrast nie einbricht. Rundum-Rand.    */
/* Alle Werte im 64-Farben-Raster (0/85/170/255), da Text nicht gedithert.   */
#define HC_DAY_FILL    GRAD_RGB(  0,  0,  0)   /* Tag: dunkle Schrift   */
#define HC_DAY_EDGE    GRAD_RGB(255,255,255)   /* Tag: heller Rand      */
#define HC_NIGHT_FILL  GRAD_RGB(255,255,255)   /* Nacht: helle Schrift  */
#define HC_NIGHT_EDGE  GRAD_RGB(  0,  0,  0)   /* Nacht: dunkler Rand   */
#define HC_EDGE_PX     2                       /* Randdicke rundum (px) */

/* ---- Persistente Astronomie-Daten vom Telefon -------------------------- */
typedef struct {
  bool    have;
  int32_t lat_mdeg;
  int32_t sun_decl_mdeg, sun_ha_mdeg;
  int32_t moon_decl_mdeg, moon_ha_mdeg;
  bool    moon_show;
  time_t  recv_epoch;
  /* Daemmerungs-Schwellen, lokale Minuten ab Mitternacht, -1 = ungueltig   */
  int16_t astro_dawn, naut_dawn, civ_dawn, sunrise, golden_rise;
  int16_t golden_set, sunset, civ_dusk, naut_dusk, astro_dusk;
  int16_t moon_rise, moon_set;
  int16_t moon_illum;   /* NEU: beleuchteter Anteil 0..100 */
  bool    moon_waxing;  /* NEU: true = zunehmend (hell rechts) */
} SolarData;

/* Wetterdaten vom Telefon, als ein Block persistiert (108 Byte < 256).
 * Neue Felder nur ans Ende, sonst liest ein Update Muell. */
typedef struct {
  uint8_t version;
  int32_t start;               /* Epoche der ersten Stunde (lokale Mitternacht) */
  int32_t recv;                /* Empfangszeit auf der Uhr                       */
  uint8_t hours[WX_HOURS];     /* Bit 0-3 Bedeckung/10, Bit 4-6 Niederschlag     */
  uint8_t wind[WX_HOURS];      /* km/h                                           */
  uint8_t temps[4];            /* Hoch/Tief Tag 0, Hoch/Tief Tag 1 (+ WX_TEMP_OFFSET) */
} WxData;

static Window *s_window;
static Layer  *s_canvas;
static SolarData s_d;
static bool s_auto_text_color;
static bool s_show_date;
static bool s_ahead_of_time;
static bool s_time_at_top;              /* NEU: Zeit oben im Himmel statt Standardposition */
static bool s_turbine_anim;             /* NEU: Windrad bei Minutenwechsel drehen */
static bool s_high_contrast;            /* NEU: gut lesbarer Modus, harter Tag/Nacht-Wechsel */
static bool s_show_moon_phase;          /* NEU: Mondscheibe mit Phase statt schlichter Scheibe */
static bool s_more_sat;                 /* NEU: kraeftigere Himmelsfarben (Tag + Daemmerung)  */
static GFont s_date_font;               /* Custom Date Font, einmal geladen */
static char s_time_buf[16];
static char s_date_buf[28];

static WxData s_wx;
static int    s_wx_mode;                /* WX_MODE_*                                   */
static bool   s_wx_show_temp;           /* Hoch/Tief in der Datumszeile                */
static time_t s_wx_last_req;            /* letzte Wetter-Anfrage an das Telefon        */

/* NEU: einmal beim Start gewuerfelter Seitenabstand fuer Baum/Haus */
static int16_t s_tree_gap;
static int16_t s_house_gap;

/* NEU: Windrad-Rotationsoffset (TRIG-Angle) und Animationszustand */
static int32_t s_turbine_angle;         /* aktueller Ruhe-/Zwischenwinkel */
static int32_t s_turbine_from, s_turbine_to;
static uint32_t s_turbine_elapsed_ms;
static AppTimer *s_turbine_timer;
#ifdef DEBUG_TIMELAPSE
static time_t s_sim_now;
#endif

/* ======================= Zeit (optional Zeitraffer) ==================== */

#ifdef DEBUG_TIMELAPSE
static time_t effective_now(void) {
  return s_sim_now;
}

static void debug_timelapse_init(void) {
  s_sim_now = time(NULL);
  if (s_d.have) s_d.recv_epoch = s_sim_now;
}

static void debug_timelapse_advance(void) {
  s_sim_now += (time_t)DEBUG_TIMELAPSE_MIN_PER_SEC * 60;
}
#else
static time_t effective_now(void) {
  return time(NULL);
}
#endif

/* ======================= Mathe-Helfer ================================== */

static int32_t mdeg_to_angle(int32_t mdeg) {
  return (int32_t)((int64_t)mdeg * TRIG_MAX_ANGLE / 360000L);
}

/* Stundenwinkel aus Auf-/Untergangs-Fortschritt (0 am Aufgang, Maximum am Zenit).
 * Unabhaengig vom letzten Telefon-Snapshot – verhindert Spruenge bei Config-Updates. */
static int32_t ha_mdeg_from_arc(int progress, int span, int64_t rate_udeg_min) {
  if (span < 1) span = 1;
  int64_t half = (int64_t)span * rate_udeg_min / 2;
  int64_t ha_udeg = (int64_t)progress * rate_udeg_min - half;
  return (int32_t)(ha_udeg / 1000);
}

static bool is_daytime_min(int now_min) {
  if (s_d.sunrise < 0 || s_d.sunset < 0) return false;
  return now_min >= s_d.sunrise && now_min <= s_d.sunset;
}

static int wrap_min(int m) {
  while (m < 0) m += 1440;
  while (m >= 1440) m -= 1440;
  return m;
}

/* Nur fuer Sonne/Mond-Bahn: Uhrzeit um AHEAD_OF_TIME_MIN vorschieben */
static int body_now_min(int now_min) {
  if (!s_ahead_of_time) return now_min;
  return wrap_min(now_min + AHEAD_OF_TIME_MIN);
}

/* ---- Hemisphaeren-Richtung Ost-West-Bewegung -----------------------------
 * Bisher lief die Bahn immer links->rechts (wie auf der Nordhalbkugel
 * gesehen, Blick nach Sueden). Auf der Suedhalbkugel (Blick nach Norden)
 * bewegen sich Sonne und Mond gegenlaeufig, also rechts->links.
 * Der Kipppunkt liegt nicht fest am Aequator, sondern dort, wo Beobachter-
 * breite und Deklination des jeweiligen Koerpers uebereinstimmen (das
 * wandert im Jahresverlauf zwischen den Wendekreisen, +-23.4 Grad).
 * hemisphere_dir_factor_pm() liefert dafuer einen Faktor in Promille:
 *   +1000 = bisheriges Verhalten (links->rechts)
 *   -1000 = gespiegelt (rechts->links)
 *    0    = Bahn steht im Zenit -> rein vertikale Bewegung
 * In einer Uebergangszone von +-EQUATOR_BAND_MDEG um den Kipppunkt wird
 * linear zwischen beiden Richtungen ueberblendet, statt hart umzuschalten,
 * damit es beim Ueberschreiten keinen sichtbaren Sprung gibt. */
#define EQUATOR_BAND_MDEG 5000  /* 5 Grad Breite der Uebergangszone */

static int32_t hemisphere_dir_factor_pm(int32_t decl_mdeg) {
  int32_t offset_mdeg = s_d.lat_mdeg - decl_mdeg;
  int32_t factor_pm = offset_mdeg * 1000 / EQUATOR_BAND_MDEG;
  if (factor_pm >  1000) factor_pm =  1000;
  if (factor_pm < -1000) factor_pm = -1000;
  return factor_pm;
}

static int32_t asin_trig(int32_t s) {
  if (s <= 0) return 0;
  if (s >= R) return TRIG_MAX_ANGLE / 4;
  int32_t lo = 0, hi = TRIG_MAX_ANGLE / 4;
  for (int i = 0; i < 16; i++) {
    int32_t mid = (lo + hi) / 2;
    if (sin_lookup(mid) < s) lo = mid; else hi = mid;
  }
  return (lo + hi) / 2;
}

static int32_t sin_alt_trig(int32_t decl_mdeg, int32_t ha_mdeg) {
  int32_t lat_a  = mdeg_to_angle(s_d.lat_mdeg);
  int32_t decl_a = mdeg_to_angle(decl_mdeg);
  int32_t ha_a   = mdeg_to_angle(ha_mdeg);
  int32_t sL = sin_lookup(lat_a),  cL = cos_lookup(lat_a);
  int32_t sD = sin_lookup(decl_a), cD = cos_lookup(decl_a);
  int32_t cH = cos_lookup(ha_a);
  int64_t t1 = (int64_t)sL * sD / R;
  int64_t t2 = (int64_t)cL * cD / R;
  t2 = t2 * cH / R;
  return (int32_t)(t1 + t2);
}

/* Hoehenwinkel in Milligrad (0..90000), -1 wenn unter dem Horizont */
static int32_t alt_mdeg_from(int32_t decl_mdeg, int32_t ha_mdeg) {
  int32_t sin_alt = sin_alt_trig(decl_mdeg, ha_mdeg);
  if (sin_alt < 0) return -1;
  if (sin_alt == 0) return 0;
  int32_t alt_a = asin_trig(sin_alt);
  return (int32_t)((int64_t)alt_a * 360000L / TRIG_MAX_ANGLE);
}

/* Y aus echtem Hoehenwinkel: 0 Grad = baseline, 90 Grad = sky_top */
static int16_t y_from_alt_mdeg(int32_t alt_mdeg, int16_t baseline, int16_t sky_top) {
  int16_t sky_h = baseline - sky_top;
  if (sky_h < 1) sky_h = 1;
  int32_t y = baseline - (int32_t)((int64_t)alt_mdeg * sky_h / 90000L);
  if (y < sky_top) y = sky_top;
  return (int16_t)y;
}

/* Auf-/Untergang-Fenster in lokalen Minuten (Mond ggf. ueber Mitternacht) */
static bool arc_time_window(int now_min, int16_t rise, int16_t set,
                            int *progress, int *span) {
  if (rise < 0 || set < 0) return false;
  if (rise <= set) {
    if (now_min < rise || now_min > set) return false;
    *span = set - rise;
    *progress = now_min - rise;
    return true;
  }
  if (now_min >= rise || now_min <= set) {
    *span = (1440 - rise) + set;
    *progress = (now_min >= rise) ? (now_min - rise) : ((1440 - rise) + now_min);
    return true;
  }
  return false;
}

/* X aus Auf-/Untergangszeit, Y aus echtem Hoehenwinkel (wie Sonne).
 * Sichtbarkeit NUR ueber Auf-/Untergangs-Fenster – einmal weg, bis zum naechsten Aufgang. */
static bool body_point_rise_set(int now_min, time_t now,
                                int32_t decl_mdeg, int32_t ref_ha_mdeg,
                                int64_t rate_udeg_min,
                                int16_t rise, int16_t set,
                                int16_t baseline, int16_t sky_top, int16_t cx, int16_t A,
                                GPoint *out) {
  (void)now;
  (void)ref_ha_mdeg;
  int progress, span;
  if (!arc_time_window(now_min, rise, set, &progress, &span)) return false;
  if (span < 1) span = 1;
  int32_t theta = (int32_t)progress * (TRIG_MAX_ANGLE / 2) / span;
  int32_t ha_mdeg = ha_mdeg_from_arc(progress, span, rate_udeg_min);
  int32_t alt_mdeg = alt_mdeg_from(decl_mdeg, ha_mdeg);
  if (alt_mdeg < 0) alt_mdeg = 0;
  int32_t dir_pm = hemisphere_dir_factor_pm(decl_mdeg);
  int64_t x_off = (int64_t)A * cos_lookup(theta) * dir_pm / R / 1000;
  out->x = (int16_t)(cx - (int32_t)x_off);
  out->y = y_from_alt_mdeg(alt_mdeg, baseline, sky_top);
  if (out->y > baseline) out->y = baseline;
  return true;
}

/* Sonne: horizontal Auf-/Untergang, vertikal echter Hoehenwinkel */
static bool sun_point(int now_min, time_t now, int32_t decl_mdeg,
                      int16_t baseline, int16_t sky_top, int16_t cx, int16_t A,
                      GPoint *out) {
  return body_point_rise_set(body_now_min(now_min), now, decl_mdeg, s_d.sun_ha_mdeg,
                             SUN_RATE_UDEG_MIN, s_d.sunrise, s_d.sunset,
                             baseline, sky_top, cx, A, out);
}

/* Mond: gleiche Logik wie Sonne (Auf-/Untergang vom Telefon) */
static bool moon_point(int now_min, time_t now, int32_t decl_mdeg,
                       int16_t baseline, int16_t sky_top, int16_t cx, int16_t A,
                       GPoint *out) {
  return body_point_rise_set(body_now_min(now_min), now, decl_mdeg, s_d.moon_ha_mdeg,
                             MOON_RATE_UDEG_MIN, s_d.moon_rise, s_d.moon_set,
                             baseline, sky_top, cx, A, out);
}

/* ======================= Himmelsfarbe ================================== */

static void lerp_pair(GradRGB t0, GradRGB h0, GradRGB t1, GradRGB h1,
                      uint16_t f, GradRGB *top, GradRGB *hor) {
  *top = grad_lerp(t0, t1, f);
  *hor = grad_lerp(h0, h1, f);
}

static void sky_colors_fallback(int now_min, GradRGB *top, GradRGB *hor) {
  if (s_d.sunrise >= 0 && s_d.sunset >= 0) {
    if (now_min >= s_d.sunrise && now_min <= s_d.sunset) {
      *top = s_more_sat ? SAT_DAY_TOP : DAY_TOP;
      *hor = s_more_sat ? SAT_DAY_HOR : DAY_HOR;
      return;
    }
    if (now_min > s_d.sunset) {
      int end = (s_d.astro_dusk >= 0) ? s_d.astro_dusk : s_d.sunset + 90;
      if (now_min <= end) {
        int span = end - s_d.sunset;
        if (span < 1) span = 1;
        uint16_t f = (uint16_t)((int32_t)(now_min - s_d.sunset) * 256 / span);
        lerp_pair(s_more_sat ? SAT_GOLD_TOP : GOLD_TOP,
                  s_more_sat ? SAT_GOLD_HOR : GOLD_HOR,
                  ASTRO_TOP, ASTRO_HOR, f, top, hor);
        return;
      }
    }
    if (now_min < s_d.sunrise) {
      int start = (s_d.astro_dawn >= 0) ? s_d.astro_dawn : s_d.sunrise - 90;
      if (now_min >= start) {
        int span = s_d.sunrise - start;
        if (span < 1) span = 1;
        uint16_t f = (uint16_t)((int32_t)(now_min - start) * 256 / span);
        lerp_pair(ASTRO_TOP, ASTRO_HOR,
                  s_more_sat ? SAT_GOLD_TOP : GOLD_TOP,
                  s_more_sat ? SAT_GOLD_HOR : GOLD_HOR,
                  f, top, hor);
        return;
      }
    }
  }
  *top = NIGHT_TOP; *hor = NIGHT_HOR;
}

static void sky_night_fade(bool to_night, int dist_min, GradRGB *top, GradRGB *hor) {
  if (dist_min >= SKY_NIGHT_FADE_MIN) {
    *top = NIGHT_TOP; *hor = NIGHT_HOR;
    return;
  }
  if (to_night) {
    uint16_t f = (uint16_t)((int32_t)dist_min * 256 / SKY_NIGHT_FADE_MIN);
    lerp_pair(ASTRO_TOP, ASTRO_HOR, NIGHT_TOP, NIGHT_HOR, f, top, hor);
  } else {
    uint16_t f = (uint16_t)((int32_t)(SKY_NIGHT_FADE_MIN - dist_min) * 256 / SKY_NIGHT_FADE_MIN);
    lerp_pair(NIGHT_TOP, NIGHT_HOR, ASTRO_TOP, ASTRO_HOR, f, top, hor);
  }
}

static void sky_colors(int now_min, GradRGB *top, GradRGB *hor) {
  int16_t km[10]; GradRGB kt[10], kh[10];
  int n = 0;
  /* NEU: Sonnenhoechststand als Kipppunkt. sunrise und sunset sind GOLD, der
   * Mittelpunkt (Sonnenhoechststand = Mitte zwischen Auf- und Untergang) ist
   * DAY. Dadurch nimmt das Orange vom Sonnenaufgang bis zum Mittag ab und danach
   * bis zum Untergang wieder zu. golden_rise/golden_set entfallen hier, der
   * V-Verlauf deckt sie ab. */
  int16_t solar_noon = (s_d.sunrise >= 0 && s_d.sunset >= 0)
                       ? (int16_t)(((int)s_d.sunrise + (int)s_d.sunset) / 2) : -1;
  #define KF(MIN, T, H) do { if ((MIN) >= 0) { km[n]=(MIN); kt[n]=(T); kh[n]=(H); n++; } } while (0)
  KF(s_d.astro_dawn,  ASTRO_TOP, ASTRO_HOR);
  if (s_more_sat) {
    KF(s_d.naut_dawn,   SAT_NAUT_TOP,  SAT_NAUT_HOR);
    KF(s_d.civ_dawn,    SAT_CIVIL_TOP, SAT_CIVIL_HOR);
    KF(s_d.sunrise,     SAT_GOLD_TOP,  SAT_GOLD_HOR);
    KF(solar_noon,      SAT_DAY_TOP,   SAT_DAY_HOR);
    KF(s_d.sunset,      SAT_GOLD_TOP,  SAT_GOLD_HOR);
    KF(s_d.civ_dusk,    SAT_CIVIL_TOP, SAT_CIVIL_HOR);
    KF(s_d.naut_dusk,   SAT_NAUT_TOP,  SAT_NAUT_HOR);
  } else {
    KF(s_d.naut_dawn,   NAUT_TOP,  NAUT_HOR);
    KF(s_d.civ_dawn,    CIVIL_TOP, CIVIL_HOR);
    KF(s_d.sunrise,     GOLD_TOP,  GOLD_HOR);
    KF(solar_noon,      DAY_TOP,   DAY_HOR);
    KF(s_d.sunset,      GOLD_TOP,  GOLD_HOR);
    KF(s_d.civ_dusk,    CIVIL_TOP, CIVIL_HOR);
    KF(s_d.naut_dusk,   NAUT_TOP,  NAUT_HOR);
  }
  KF(s_d.astro_dusk,  ASTRO_TOP, ASTRO_HOR);
  #undef KF

  if (n == 0 || n == 1) { sky_colors_fallback(now_min, top, hor); return; }

  bool astro_valid = (s_d.astro_dawn >= 0 && s_d.astro_dusk >= 0);
  if (astro_valid && now_min > s_d.astro_dusk) {
    sky_night_fade(true, now_min - s_d.astro_dusk, top, hor);
    return;
  }
  if (astro_valid && now_min < s_d.astro_dawn) {
    sky_night_fade(false, s_d.astro_dawn - now_min, top, hor);
    return;
  }

  /* NEU: Keyframes als 24h-Zyklus. Schwellen, die ueber Mitternacht rutschen
   * (z.B. nautische Daemmerung im Hochsommer), auf den Folgetag schieben, damit
   * die Liste monoton bleibt. Frueher fiel genau dieser Fall (km[n-1] <= km[0])
   * auf den groben sunrise/sunset-Fallback zurueck und verschluckte die feinen
   * Stufen wie die goldene Stunde. */
  int32_t um[11];
  um[0] = km[0];
  for (int i = 1; i < n; i++) {
    um[i] = km[i];
    while (um[i] < um[i - 1]) um[i] += 1440;
  }
  um[n] = um[0] + 1440;                   /* Zyklus schliessen: letzter -> erster */

  int32_t cand[2] = { now_min, now_min + 1440 };
  for (int c = 0; c < 2; c++) {
    int32_t t = cand[c];
    if (t < um[0] || t > um[n]) continue;
    for (int i = 0; i < n; i++) {
      if (t >= um[i] && t <= um[i + 1]) {
        int hi = (i + 1 == n) ? 0 : i + 1;     /* letzter KF blendet zum ersten */
        int32_t span = um[i + 1] - um[i];
        if (span < 1) span = 1;
        uint16_t f = (uint16_t)((int32_t)(t - um[i]) * 256 / span);
        lerp_pair(kt[i], kh[i], kt[hi], kh[hi], f, top, hor);
        return;
      }
    }
  }
  *top = kt[0]; *hor = kh[0];             /* unerreichbar, nur zur Sicherheit */
}

/* ======================= Wetter ======================================== */

static bool wx_active(void) {
  return s_wx_mode != WX_MODE_OFF && s_wx.version == WX_DATA_VERSION && s_wx.start != 0;
}

/* Stundenindex fuer einen Zeitpunkt, -1 ausserhalb der Daten */
static int wx_index_at(time_t t) {
  if (!wx_active()) return -1;
  int32_t d = (int32_t)(t - (time_t)s_wx.start);
  if (d < 0) return -1;
  int i = d / 3600;
  return (i < WX_HOURS) ? i : -1;
}

static int wx_cloud_pct(uint8_t b) { return (b & 0x0F) * 10; }
static int wx_precip(uint8_t b)    { return (b >> 4) & 0x07; }

/* Wind der laufenden Stunde in km/h, -1 ohne Wetterdaten */
static int wx_wind_now(void) {
  int i = wx_index_at(effective_now());
  return (i < 0) ? -1 : s_wx.wind[i];
}

/* Mitternacht des Tages, dessen Wetter gezeigt wird: heute bis Sonnen-
 * untergang, danach morgen. */
static time_t wx_forecast_day(time_t now, int now_min, bool *tomorrow) {
  struct tm *lt = localtime(&now);
  time_t midnight = now - (lt->tm_hour * 3600 + lt->tm_min * 60 + lt->tm_sec);
  *tomorrow = (s_d.sunset >= 0 && now_min > s_d.sunset);
  return *tomorrow ? midnight + SECONDS_PER_DAY : midnight;
}

/* Hoch/Tief fuer den Vorhersagetag; false wenn unbekannt */
static bool wx_hi_lo(time_t now, int now_min, int *hi, int *lo) {
  if (!wx_active()) return false;
  bool tomorrow;
  time_t day = wx_forecast_day(now, now_min, &tomorrow);
  /* +12 h: robust gegen Sommerzeit und eine nicht ganz mitternaechtliche
   * Startstunde */
  int32_t d = (int32_t)(day + 12 * 3600 - (time_t)s_wx.start);
  if (d < 0) return false;
  int k = d / SECONDS_PER_DAY;
  if (k > 1) return false;
  uint8_t h = s_wx.temps[k * 2], l = s_wx.temps[k * 2 + 1];
  if (h == 0 || l == 0) return false;
  *hi = (int)h - WX_TEMP_OFFSET;
  *lo = (int)l - WX_TEMP_OFFSET;
  return true;
}

/* Grauton gleicher Helligkeit, etwas abgedunkelt */
static GradRGB wx_overcast(GradRGB c) {
  int l = (c.r * 77 + c.g * 150 + c.b * 29) >> 8;
  l = l * WX_OVERCAST_DIM_PCT / 100;
  return GRAD_RGB(l, l, l);
}

/* Aktuelle Bewoelkung vergraut den Verlauf, Nebel hellt den Horizont auf.
 * Sonne und Mond werden danach gezeichnet und bleiben immer sichtbar. */
static void wx_tint_sky(time_t now, GradRGB *top, GradRGB *hor) {
  int i = wx_index_at(now);
  if (i < 0) return;
  int cc = wx_cloud_pct(s_wx.hours[i]);
  int p  = wx_precip(s_wx.hours[i]);
  int f = 0;
  if (cc > WX_TINT_FROM_PCT) {
    f = (cc - WX_TINT_FROM_PCT) * WX_TINT_MAX / (100 - WX_TINT_FROM_PCT);
  }
  if (p == WX_P_RAIN || p == WX_P_SNOW || p == WX_P_THUNDER) f += WX_PRECIP_TINT_EXTRA;
  if (f > 256) f = 256;
  if (f > 0) {
    *top = grad_lerp(*top, wx_overcast(*top), (uint16_t)f);
    *hor = grad_lerp(*hor, wx_overcast(*hor), (uint16_t)f);
  }
  if (p == WX_P_FOG) {
    GradRGB fog = grad_lerp(wx_overcast(*hor), GRAD_RGB(255, 255, 255), 96);
    *hor = grad_lerp(*hor, fog, WX_FOG_MIX);
  }
}

/* ======================= Textfarbe (Auto-Cycle) ======================== */

static void lerp_text(GradRGB f0, GradRGB s0, GradRGB f1, GradRGB s1,
                      uint16_t f, GradRGB *fill, GradRGB *shad) {
  *fill = grad_lerp(f0, f1, f);
  *shad = grad_lerp(s0, s1, f);
}

static void text_colors_fallback(int now_min, GradRGB *fill, GradRGB *shad) {
  if (s_d.sunrise >= 0 && s_d.sunset >= 0) {
    if (now_min >= s_d.sunrise && now_min <= s_d.sunset) {
      *fill = TF_DAY; *shad = TS_DAY;
      return;
    }
    if (now_min > s_d.sunset) {
      int end = (s_d.astro_dusk >= 0) ? s_d.astro_dusk : s_d.sunset + 90;
      if (now_min <= end) {
        int span = end - s_d.sunset;
        if (span < 1) span = 1;
        uint16_t f = (uint16_t)((int32_t)(now_min - s_d.sunset) * 256 / span);
        lerp_text(TF_GOLD, TS_GOLD, TF_ASTRO, TS_ASTRO, f, fill, shad);
        return;
      }
    }
    if (now_min < s_d.sunrise) {
      int start = (s_d.astro_dawn >= 0) ? s_d.astro_dawn : s_d.sunrise - 90;
      if (now_min >= start) {
        int span = s_d.sunrise - start;
        if (span < 1) span = 1;
        uint16_t f = (uint16_t)((int32_t)(now_min - start) * 256 / span);
        lerp_text(TF_ASTRO, TS_ASTRO, TF_GOLD, TS_GOLD, f, fill, shad);
        return;
      }
    }
  }
  *fill = TF_NIGHT; *shad = TS_NIGHT;
}

static void text_night_fade(bool to_night, int dist_min, GradRGB *fill, GradRGB *shad) {
  if (dist_min >= SKY_NIGHT_FADE_MIN) {
    *fill = TF_NIGHT; *shad = TS_NIGHT;
    return;
  }
  if (to_night) {
    uint16_t f = (uint16_t)((int32_t)dist_min * 256 / SKY_NIGHT_FADE_MIN);
    lerp_text(TF_ASTRO, TS_ASTRO, TF_NIGHT, TS_NIGHT, f, fill, shad);
  } else {
    uint16_t f = (uint16_t)((int32_t)(SKY_NIGHT_FADE_MIN - dist_min) * 256 / SKY_NIGHT_FADE_MIN);
    lerp_text(TF_NIGHT, TS_NIGHT, TF_ASTRO, TS_ASTRO, f, fill, shad);
  }
}

static void text_colors(int now_min, GradRGB *fill, GradRGB *shad) {
  int16_t km[10];
  GradRGB kf[10], ks[10];
  int n = 0;
  /* NEU: gleicher Sonnenhoechststand-Kipppunkt wie in sky_colors */
  int16_t solar_noon = (s_d.sunrise >= 0 && s_d.sunset >= 0)
                       ? (int16_t)(((int)s_d.sunrise + (int)s_d.sunset) / 2) : -1;
  #define KFT(MIN, F, S) do { if ((MIN) >= 0) { km[n]=(MIN); kf[n]=(F); ks[n]=(S); n++; } } while (0)
  KFT(s_d.astro_dawn,  TF_ASTRO, TS_ASTRO);
  KFT(s_d.naut_dawn,   TF_NAUT,  TS_NAUT);
  KFT(s_d.civ_dawn,    TF_CIVIL, TS_CIVIL);
  KFT(s_d.sunrise,     TF_GOLD,  TS_GOLD);
  KFT(solar_noon,      TF_DAY,   TS_DAY);    /* NEU: Kipppunkt Mittag */
  KFT(s_d.sunset,      TF_GOLD,  TS_GOLD);
  KFT(s_d.civ_dusk,    TF_CIVIL, TS_CIVIL);
  KFT(s_d.naut_dusk,   TF_NAUT,  TS_NAUT);
  KFT(s_d.astro_dusk,  TF_ASTRO, TS_ASTRO);
  #undef KFT

  if (n == 0 || n == 1) { text_colors_fallback(now_min, fill, shad); return; }

  bool astro_valid = (s_d.astro_dawn >= 0 && s_d.astro_dusk >= 0);
  if (astro_valid && now_min > s_d.astro_dusk) {
    text_night_fade(true, now_min - s_d.astro_dusk, fill, shad);
    return;
  }
  if (astro_valid && now_min < s_d.astro_dawn) {
    text_night_fade(false, s_d.astro_dawn - now_min, fill, shad);
    return;
  }

  /* NEU: gleiche 24h-Zyklus-Behandlung wie in sky_colors (Mitternachts-Wrap) */
  int32_t um[11];
  um[0] = km[0];
  for (int i = 1; i < n; i++) {
    um[i] = km[i];
    while (um[i] < um[i - 1]) um[i] += 1440;
  }
  um[n] = um[0] + 1440;

  int32_t cand[2] = { now_min, now_min + 1440 };
  for (int c = 0; c < 2; c++) {
    int32_t t = cand[c];
    if (t < um[0] || t > um[n]) continue;
    for (int i = 0; i < n; i++) {
      if (t >= um[i] && t <= um[i + 1]) {
        int hi = (i + 1 == n) ? 0 : i + 1;
        int32_t span = um[i + 1] - um[i];
        if (span < 1) span = 1;
        uint16_t f = (uint16_t)((int32_t)(t - um[i]) * 256 / span);
        lerp_text(kf[i], ks[i], kf[hi], ks[hi], f, fill, shad);
        return;
      }
    }
  }
  *fill = kf[0]; *shad = ks[0];
}

static GColor grad_rgb_to_gcolor(GradRGB c) {
#if defined(PBL_COLOR)
  return GColorFromRGB(c.r, c.g, c.b);
#else
  return ((uint16_t)c.r + c.g + c.b) > 384 ? GColorWhite : GColorBlack;
#endif
}

/* ======================= Zeichnen ====================================== */

#if defined(PBL_COLOR)
  #define SUN_BORDER_COLOR  GColorFromRGB(110, 0, 0)
#else
  #define SUN_BORDER_COLOR  GColorBlack
#endif

static void draw_disc(GContext *ctx, GPoint c, int16_t r,
                      GColor fill, GColor border) {
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_circle(ctx, c, r);
  graphics_context_set_stroke_color(ctx, border);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, c, r);
}

/* ═══ Mondscheibe mit Phase (uebernommen aus SolarPath, an solararc angepasst) ═══
 * illum    : beleuchteter Anteil 0..100
 * limb_ang : Kippwinkel des Terminators (Pebble-Trig). TRIG_MAX_ANGLE/4 = hell
 *            rechts (zunehmend), 3*TRIG_MAX_ANGLE/4 = hell links (abnehmend).
 * border   : Randfarbe der Scheibe. */
static int isqrt_i(int v) {
  if (v <= 0) return 0;
  int x = v, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + v / x) / 2; }
  return x;
}

static void draw_moon(GContext *ctx, GPoint c, int radius, int illum,
                      int32_t limb_ang, GColor border) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, c, radius);
  int32_t s  = sin_lookup(limb_ang);
  int32_t co = cos_lookup(limb_ang);
  int     R2 = radius * radius;
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 1);
  for (int py = -radius; py <= radius; py++) {
    for (int px = -radius; px <= radius; px++) {
      if (px * px + py * py > R2) continue;
      int u  = (int)(((int32_t)px * s  - (int32_t)py * co) / TRIG_MAX_RATIO);
      int v  = (int)(((int32_t)px * co + (int32_t)py * s)  / TRIG_MAX_RATIO);
      int hw = isqrt_i(R2 - v * v);
      if (100 * u >= hw * (100 - 2 * illum)) {
        graphics_draw_pixel(ctx, GPoint(c.x + px, c.y + py));
      }
    }
  }
  graphics_context_set_stroke_color(ctx, border);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, c, radius);
}

static void draw_bodies(GContext *ctx, int now_min, time_t now,
                        int16_t H, int16_t baseline, int16_t sky_top,
                        int16_t cx, int16_t A_sun, int16_t A_moon) {
  if (!s_d.have) return;
  GPoint p;
  if (sun_point(now_min, now, s_d.sun_decl_mdeg, baseline, sky_top, cx, A_sun, &p)) {
    draw_disc(ctx, p, H * SUN_R_PCT / 100, GColorChromeYellow, SUN_BORDER_COLOR);
  }
  if (s_d.moon_show) {
    if (moon_point(now_min, now, s_d.moon_decl_mdeg, baseline, sky_top, cx, A_moon, &p)) {
      GColor moon_border = is_daytime_min(now_min) ? GColorBlack : GColorWhite;
      if (s_show_moon_phase) {
        /* Terminator-Ausrichtung ist hemisphaerenabhaengig: auf der
         * Suedhalbkugel (Blick nach Norden) erscheint die Mondscheibe um
         * 180 Grad gedreht gegenueber der Nordhalbkugel (Blick nach Sueden) -
         * "hell rechts" bei zunehmendem Mond gilt nur noerdlich des
         * Kipppunkts. Gleicher Kipppunkt wie bei der Bahnrichtung des
         * Mondes (Breite vs. Monddeklination), s. hemisphere_dir_factor_pm(). */
        int32_t limb = s_d.moon_waxing ? (TRIG_MAX_ANGLE / 4) : (TRIG_MAX_ANGLE * 3 / 4);
        if (hemisphere_dir_factor_pm(s_d.moon_decl_mdeg) < 0) {
          limb += TRIG_MAX_ANGLE / 2;
          if (limb >= TRIG_MAX_ANGLE) limb -= TRIG_MAX_ANGLE;
        }
        draw_moon(ctx, p, H * MOON_R_PCT / 100, s_d.moon_illum, limb, moon_border);
      } else {
        draw_disc(ctx, p, H * MOON_R_PCT / 100, GColorLightGray, moon_border);
      }
    }
  }
}

/* ---- Wolken auf der Sonnenbahn -------------------------------------------
 * Jede Vorhersagestunde mit genug Bedeckung bekommt eine Wolke an der Stelle
 * der Bahn, an der die Sonne zu dieser Stunde steht. Vergangene Stunden
 * fallen weg, die Sonne laeuft also sichtbar in ihr Wetter hinein.
 *
 * Form: vier Kreise ueber einem flachen Sockel, Masse in Promille der
 * Wolkengroesse u, um den Bahnpunkt zentriert. Gezeichnet wird in drei
 * Durchgaengen ueber ALLE Wolken (Rand, Schatten, Licht), damit benachbarte
 * Stunden zu einer Wolkenbank ohne innere Raender verschmelzen. */
static const int16_t CLOUD_LOBES[4][3] = {   /* dx, dy, r */
  { -900,  200, 550 },
  { -250,  -50, 700 },
  {  500,   50, 600 },
  { 1050,  350, 400 },
};
#define CLOUD_BASE_X0   -900
#define CLOUD_BASE_X1   1050
#define CLOUD_BASE_Y0    200
#define CLOUD_BASE_Y1    750      /* flache Unterkante */
#define CLOUD_LIGHT_PM   300      /* Lichtkreis um 30 % des Radius nach oben */

typedef struct {
  GPoint  c;
  int16_t u;
  uint8_t precip;
} WxCloud;

enum { CLOUD_PASS_EDGE, CLOUD_PASS_SHADE, CLOUD_PASS_LIGHT };

static void cloud_pass(GContext *ctx, const WxCloud *w, int pass) {
  int16_t u = w->u;
  for (int k = 0; k < 4; k++) {
    int16_t x = w->c.x + CLOUD_LOBES[k][0] * u / 1000;
    int16_t y = w->c.y + CLOUD_LOBES[k][1] * u / 1000;
    int16_t r = CLOUD_LOBES[k][2] * u / 1000;
    if (pass == CLOUD_PASS_EDGE) r += 1;
    if (pass == CLOUD_PASS_LIGHT) {
      /* kleinerer Kreis, oben buendig: bleibt innerhalb des Schattenkreises */
      int16_t d = r * CLOUD_LIGHT_PM / 1000;
      y -= d;
      r -= d;
    }
    if (r > 0) graphics_fill_circle(ctx, GPoint(x, y), r);
  }
  if (pass == CLOUD_PASS_LIGHT) return;
  int16_t g = (pass == CLOUD_PASS_EDGE) ? 1 : 0;
  int16_t x0 = w->c.x + CLOUD_BASE_X0 * u / 1000 - g;
  int16_t x1 = w->c.x + CLOUD_BASE_X1 * u / 1000 + g;
  int16_t y0 = w->c.y + CLOUD_BASE_Y0 * u / 1000 - g;
  int16_t y1 = w->c.y + CLOUD_BASE_Y1 * u / 1000 + g;
  graphics_fill_rect(ctx, GRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1), 0, GCornerNone);
}

static GColor cloud_color(int pass, bool wet, bool day) {
  if (day) {
    if (pass == CLOUD_PASS_EDGE)  return wet ? CLOUD_DAY_WET_EDGE  : CLOUD_DAY_EDGE;
    if (pass == CLOUD_PASS_SHADE) return wet ? CLOUD_DAY_WET_SHADE : CLOUD_DAY_SHADE;
    return wet ? CLOUD_DAY_WET_FILL : CLOUD_DAY_FILL;
  }
  if (pass == CLOUD_PASS_EDGE)  return wet ? CLOUD_NIGHT_WET_EDGE  : CLOUD_NIGHT_EDGE;
  if (pass == CLOUD_PASS_SHADE) return wet ? CLOUD_NIGHT_WET_SHADE : CLOUD_NIGHT_SHADE;
  return wet ? CLOUD_NIGHT_WET_FILL : CLOUD_NIGHT_FILL;
}

/* Regenstriche, Schneeflocken oder Blitz unter der Wolke */
static void draw_precip(GContext *ctx, const WxCloud *w, bool day) {
  int16_t u = w->u;
  int16_t yb = w->c.y + CLOUD_BASE_Y1 * u / 1000 + 3;
  int16_t len = u * 6 / 10;
  if (len < 4) len = 4;
  static const int16_t XS[3] = { -600, 0, 600 };
  if (w->precip == WX_P_RAIN) {
    graphics_context_set_stroke_color(ctx, day ? RAIN_DAY_COLOR : RAIN_NIGHT_COLOR);
    graphics_context_set_stroke_width(ctx, 1);
    for (int k = 0; k < 3; k++) {
      int16_t x = w->c.x + XS[k] * u / 1000;
      int16_t y = yb + (k == 1 ? 2 : 0);
      graphics_draw_line(ctx, GPoint(x, y), GPoint(x - len / 3, y + len));
    }
  } else if (w->precip == WX_P_SNOW) {
    graphics_context_set_fill_color(ctx, day ? SNOW_DAY_COLOR : SNOW_NIGHT_COLOR);
    for (int k = 0; k < 3; k++) {
      int16_t x = w->c.x + XS[k] * u / 1000;
      int16_t y = yb + 1 + (k == 1 ? len / 2 : 0);
      graphics_fill_rect(ctx, GRect(x - 1, y, 3, 3), 0, GCornerNone);
      graphics_fill_rect(ctx, GRect(x - 1 - len / 4, y + len / 2 + 1, 3, 3), 0, GCornerNone);
    }
  } else if (w->precip == WX_P_THUNDER) {
    int16_t x = w->c.x;
    GPoint z[4] = {
      GPoint(x + 2, yb - 2), GPoint(x - 3, yb + len / 2 + 1),
      GPoint(x + 3, yb + len / 2 - 1), GPoint(x - 2, yb + len + 2),
    };
    graphics_context_set_stroke_color(ctx, BOLT_COLOR);
    graphics_context_set_stroke_width(ctx, 2);
    for (int k = 0; k < 3; k++) graphics_draw_line(ctx, z[k], z[k + 1]);
  }
}

/* Rand, Schatten, Licht ueber alle Wolken, dann der Niederschlag */
static void draw_cloud_set(GContext *ctx, const WxCloud *cl, int n, bool daylight) {
  for (int pass = CLOUD_PASS_EDGE; pass <= CLOUD_PASS_LIGHT; pass++) {
    for (int k = 0; k < n; k++) {
      graphics_context_set_fill_color(ctx, cloud_color(pass, cl[k].precip != WX_P_NONE, daylight));
      cloud_pass(ctx, &cl[k], pass);
    }
  }
  for (int k = 0; k < n; k++) draw_precip(ctx, &cl[k], daylight);
}

static void draw_forecast_clouds(GContext *ctx, int now_min, time_t now,
                                 int16_t H, int16_t baseline, int16_t sky_top,
                                 int16_t cx, int16_t A_sun) {
  if (s_wx_mode != WX_MODE_CLOUDS || !wx_active() || !s_d.have) return;
  int16_t rise = s_d.sunrise, set = s_d.sunset;
  if (rise < 0 || set < 0 || rise >= set) return;   /* Polartag/-nacht: keine Bahn */

  bool tomorrow;
  time_t day = wx_forecast_day(now, now_min, &tomorrow);
  int16_t sun_r = H * SUN_R_PCT / 100;

  /* static statt auf dem Stack: canvas_update bekommt die Funktion inline,
   * und auf basalt sprengten die 192 Byte zusammen mit der Textausgabe
   * den App-Stack - die Uhr hing danach komplett. */
  static WxCloud cl[WX_MAX_CLOUDS];
  int n = 0;
  for (int h = rise / 60; h <= set / 60 && n < WX_MAX_CLOUDS; h++) {
    int a = h * 60, e = a + 60;
    if (a < rise) a = rise;
    if (e > set)  e = set;
    if (!tomorrow && e <= now_min) continue;          /* schon vorbei */
    if (e - a < WX_SLOT_MIN_MIN) continue;
    int i = wx_index_at(day + h * 3600 + 1800);
    if (i < 0) continue;
    int cc = wx_cloud_pct(s_wx.hours[i]);
    int p  = wx_precip(s_wx.hours[i]);
    if (p == WX_P_FOG) p = WX_P_NONE;                 /* Nebel zeigt der Horizont */
    if (p != WX_P_NONE && cc < WX_CLOUD_MIN_PCT) cc = WX_CLOUD_MIN_PCT;
    if (cc < WX_CLOUD_MIN_PCT) continue;

    GPoint pt;
    if (!body_point_rise_set((a + e) / 2, now, s_d.sun_decl_mdeg, s_d.sun_ha_mdeg,
                             SUN_RATE_UDEG_MIN, rise, set,
                             baseline, sky_top, cx, A_sun, &pt)) continue;
    /* leicht versetzt, damit eine Wolkenbank nicht wie eine Perlenkette wirkt */
    pt.y += (h & 1) ? -sun_r / 4 : sun_r / 5;
    cl[n].c = pt;
    cl[n].u = sun_r * (cc >= WX_CLOUD_BIG_PCT ? WX_CLOUD_BIG_U_PCT : WX_CLOUD_SMALL_U_PCT) / 100;
    cl[n].precip = (uint8_t)p;
    n++;
  }
  draw_cloud_set(ctx, cl, n, is_daytime_min(now_min));
}

/* ---- Stundenstreifen: Vorstunde / jetzt / naechste Stunde ---------------
 * Feste Stelle im freien Himmel statt auf der Bahn: oben ueber dem Datum,
 * bei "Time at top" in der Mitte. Unter WX_STRIP_FROM_PCT bleibt der Platz
 * leer - leer heisst klar. Ein Sonnensymbol gibt es bewusst nicht, es
 * wuerde mit der echten Sonne konkurrieren. */
static void draw_hour_strip(GContext *ctx, int now_min, time_t now, int16_t H, int16_t cx) {
  if (s_wx_mode != WX_MODE_STRIP || !wx_active()) return;
  int16_t sun_r = H * SUN_R_PCT / 100;
  int16_t cy = s_time_at_top ? STRIP_MID_CY : STRIP_TOP_CY;

  /* Nachbarstunden zuerst, die aktuelle Stunde liegt obenauf */
  static const int8_t OFFS[3] = { -1, 1, 0 };
  static WxCloud cl[3];                     /* static: siehe draw_forecast_clouds */
  int n = 0;
  for (int k = 0; k < 3; k++) {
    int i = wx_index_at(now + OFFS[k] * 3600);
    if (i < 0) continue;
    int cc = wx_cloud_pct(s_wx.hours[i]);
    int p  = wx_precip(s_wx.hours[i]);
    if (p == WX_P_FOG) p = WX_P_NONE;                 /* Nebel zeigt der Horizont */
    if (p != WX_P_NONE && cc < WX_CLOUD_MIN_PCT) cc = WX_CLOUD_MIN_PCT;
    if (cc < WX_STRIP_FROM_PCT) continue;
    int pct = (OFFS[k] == 0) ? WX_STRIP_NOW_U_PCT : WX_STRIP_SIDE_U_PCT;
    if (cc < WX_CLOUD_MIN_PCT) pct = pct * WX_STRIP_THIN_PCT / 100;
    cl[n].c = GPoint(cx + OFFS[k] * STRIP_DX, cy);
    cl[n].u = sun_r * pct / 100;
    cl[n].precip = (uint8_t)p;
    n++;
  }
  draw_cloud_set(ctx, cl, n, is_daytime_min(now_min));
}

static void draw_ground(GContext *ctx, GRect b, int now_min) {
  (void)now_min;   /* Fenster-Tag/Nacht entfernt, Parameter bleibt fuer API-Symmetrie */
  int16_t W = b.size.w, H = b.size.h;
  int16_t baseline = H - H * GROUND_PCT / 100;
  int16_t amp = H * HILL_AMP_PCT / 100;

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 1);
  for (int16_t x = 0; x < W; x++) {
    int32_t ang = (int32_t)((int64_t)x * TRIG_MAX_ANGLE * HILL_BUMPS / W);
    int32_t norm = (sin_lookup(ang) + R) / 2;           /* 0..R */
    int16_t top = (int16_t)(baseline - (int32_t)amp * norm / R);
    graphics_draw_line(ctx, GPoint(x, top), GPoint(x, H));
  }

  graphics_context_set_fill_color(ctx, GColorBlack);

  /* Baum (links vom Windrad, Abstand einmal beim Start leicht zufaellig) */
  int16_t tx = W / 2 - s_tree_gap;                       /* NEU: s_tree_gap */
  int16_t tcy = baseline - H * TREE_FOLIAGE_CY_PCT / 100;
  int16_t tr = H * TREE_R_PCT / 100;
  graphics_fill_rect(ctx, GRect(tx - 2, tcy, 5, baseline - tcy), 0, GCornerNone);
  graphics_fill_circle(ctx, GPoint(tx, tcy), tr);
  graphics_fill_circle(ctx, GPoint(tx - tr + 2, tcy + 3), tr - 3);
  graphics_fill_circle(ctx, GPoint(tx + tr - 2, tcy + 3), tr - 3);

  /* Windrad */
  int16_t hubx = W / 2;
  int16_t huby = baseline - H * TURB_HUB_PCT / 100;
  int16_t blade = H * TURB_BLADE_PCT / 100;
  graphics_fill_rect(ctx, GRect(hubx - 2, huby, 5, baseline - huby), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 3);
  for (int k = 0; k < 3; k++) {
    int32_t a = (int32_t)TRIG_MAX_ANGLE * k / 3 + s_turbine_angle;  /* NEU: + Rotationsoffset */
    int16_t bx = (int16_t)(hubx + (int32_t)blade * sin_lookup(a) / R);
    int16_t by = (int16_t)(huby - (int32_t)blade * cos_lookup(a) / R);
    graphics_draw_line(ctx, GPoint(hubx, huby), GPoint(bx, by));
  }
  graphics_fill_circle(ctx, GPoint(hubx, huby), 3);

  /* Haus (rechts vom Windrad, Abstand einmal beim Start leicht zufaellig) */
  int16_t hx = W / 2 + s_house_gap;                      /* NEU: s_house_gap */
  int16_t hw = W * HOUSE_W_PCT / 100;
  int16_t bh = H * HOUSE_BODY_PCT / 100;
  int16_t rh = H * HOUSE_ROOF_PCT / 100;
  int16_t body_top = baseline - bh;
  graphics_fill_rect(ctx, GRect(hx - hw / 2, body_top, hw, bh), 0, GCornerNone);
  /* Giebeldach als Dreieck via Zeilen */
  int16_t apex_y = body_top - rh;
  for (int16_t y = apex_y; y <= body_top; y++) {
    int16_t half = (int16_t)((int32_t)(hw / 2 + 3) * (y - apex_y) / (rh < 1 ? 1 : rh));
    graphics_draw_line(ctx, GPoint(hx - half, y), GPoint(hx + half, y));
  }
}

static void format_time_string(time_t now) {
  struct tm *lt = localtime(&now);
  if (clock_is_24h_style()) {
    snprintf(s_time_buf, sizeof(s_time_buf), "%02d:%02d", lt->tm_hour, lt->tm_min);
  } else {
    int h = lt->tm_hour % 12;
    if (h == 0) h = 12;
    snprintf(s_time_buf, sizeof(s_time_buf), "%d:%02d", h, lt->tm_min);
  }
}

static void format_date_string(time_t now) {
  static const char *WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  struct tm *lt = localtime(&now);
  snprintf(s_date_buf, sizeof(s_date_buf), "%s   %2d", WD[lt->tm_wday], lt->tm_mday);
}

/* Freie Breite der Datumszeile. Rund: Kreissehne auf Hoehe der Zeile. */
#define DATE_SIDE_PAD_PX  6
static int16_t date_line_width(GRect b) {
#if defined(PBL_ROUND)
  int16_t r  = b.size.w / 2;
  int16_t dy = b.size.h / 2 - (DATE_Y + DATE_BOX_H / 3);
  if (dy < 0) dy = -dy;
  if (dy >= r) return 0;
  return 2 * isqrt_i(r * r - dy * dy) - 2 * DATE_SIDE_PAD_PX;
#else
  return b.size.w - 2 * DATE_SIDE_PAD_PX;
#endif
}

static bool date_line_fits(GFont font, int16_t avail) {
  GSize s = graphics_text_layout_get_content_size(s_date_buf, font, GRect(0, 0, 400, 60),
                                                  GTextOverflowModeFill, GTextAlignmentCenter);
  return s.w <= avail;
}

/* Datum und/oder Hoch/Tief nach s_date_buf. Faellt bei Platzmangel auf
 * kuerzere Formen zurueck, zuerst ohne Wochentag. false = keine Zeile. */
static bool compose_date_line(time_t now, int now_min, GFont font, GRect b) {
  int hi, lo;
  bool temps = s_wx_show_temp && wx_hi_lo(now, now_min, &hi, &lo);
  if (!temps) {
    if (!s_show_date) return false;
    format_date_string(now);
    return true;
  }
  if (!s_show_date) {
    snprintf(s_date_buf, sizeof(s_date_buf), "%d\xc2\xb0 / %d\xc2\xb0", hi, lo);
    return true;
  }
  static const char *WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  struct tm *lt = localtime(&now);
  int16_t avail = date_line_width(b);
  snprintf(s_date_buf, sizeof(s_date_buf), "%s %d  %d\xc2\xb0/%d\xc2\xb0",
           WD[lt->tm_wday], lt->tm_mday, hi, lo);
  if (date_line_fits(font, avail)) return true;
  snprintf(s_date_buf, sizeof(s_date_buf), "%d  %d\xc2\xb0/%d\xc2\xb0", lt->tm_mday, hi, lo);
  if (date_line_fits(font, avail)) return true;
  snprintf(s_date_buf, sizeof(s_date_buf), "%d\xc2\xb0/%d\xc2\xb0", hi, lo);
  return true;
}

static void draw_outlined_text(GContext *ctx, const char *text, GFont font, GRect box,
                               GColor fill, GColor sh1, GColor sh2) {
  GRect sr2 = box; sr2.origin.x += 2; sr2.origin.y += 2;
  GRect sr1 = box; sr1.origin.x += 1; sr1.origin.y += 1;
  graphics_context_set_text_color(ctx, sh2);
  graphics_draw_text(ctx, text, font, sr2, GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, sh1);
  graphics_draw_text(ctx, text, font, sr1, GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, fill);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
}

/* NEU: Text mit umlaufendem Rand (Block-Versatz +/- r in alle Richtungen),
 * damit der Rand die Glyphe komplett umschliesst, nicht nur als Schlagschatten. */
static void draw_ring_text(GContext *ctx, const char *text, GFont font, GRect box,
                           GColor fill, GColor edge, int r) {
  graphics_context_set_text_color(ctx, edge);
  for (int oy = -r; oy <= r; oy++) {
    for (int ox = -r; ox <= r; ox++) {
      if (ox == 0 && oy == 0) continue;
      GRect e = box; e.origin.x += ox; e.origin.y += oy;
      graphics_draw_text(ctx, text, font, e, GTextOverflowModeFill,
                         GTextAlignmentCenter, NULL);
    }
  }
  graphics_context_set_text_color(ctx, fill);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
}

/* NEU: Tag = heller Himmel zwischen Ende der goldenen Stunde (Morgen) und
 * Beginn der goldenen Stunde (Abend). Fallback auf Sonnenauf-/untergang, sonst
 * feste 7..19 Uhr. */
static bool hc_is_day(int now_min) {
  int16_t rise = (s_d.golden_rise >= 0) ? s_d.golden_rise : s_d.sunrise;
  int16_t set  = (s_d.golden_set  >= 0) ? s_d.golden_set  : s_d.sunset;
  if (rise >= 0 && set >= 0) {
    if (rise <= set) return now_min >= rise && now_min <= set;
    return now_min >= rise || now_min <= set;   /* ueber Mitternacht */
  }
  return now_min >= 7 * 60 && now_min < 19 * 60;
}

static void draw_clock(GContext *ctx, GRect b, time_t now, int now_min) {
  /* NEU: High-Contrast-Modus hat Vorrang. Harter Wechsel, Rundum-Rand. */
  if (s_high_contrast) {
    bool day = hc_is_day(now_min);
    GColor fill = grad_rgb_to_gcolor(day ? HC_DAY_FILL : HC_NIGHT_FILL);
    GColor edge = grad_rgb_to_gcolor(day ? HC_DAY_EDGE : HC_NIGHT_EDGE);
    GFont tf = fonts_get_system_font(FONT_TIME);
    GFont df = s_date_font;
    if (compose_date_line(now, now_min, df, b)) {
      draw_ring_text(ctx, s_date_buf, df,
                     GRect(0, DATE_Y, b.size.w, DATE_BOX_H + 4), fill, edge, HC_EDGE_PX-1);
    }
    int hy = s_time_at_top ? TIME_TOP_Y : TIME_BOTTOM_Y;
    format_time_string(now);
    draw_ring_text(ctx, s_time_buf, tf,
                   GRect(0, hy, b.size.w, TIME_BOX_H + 8), fill, edge, HC_EDGE_PX);
    return;
  }

  GradRGB fill_rgb, shad_rgb;
  if (s_auto_text_color && s_d.have) {
    text_colors(now_min, &fill_rgb, &shad_rgb);
  } else if (s_auto_text_color) {
    if (now_min >= 7 * 60 && now_min < 19 * 60) {
      fill_rgb = TF_DAY; shad_rgb = TS_DAY;
    } else {
      fill_rgb = TF_NIGHT; shad_rgb = TS_NIGHT;
    }
  } else {
    fill_rgb = GRAD_RGB(255, 255, 255);
    shad_rgb = GRAD_RGB(0, 0, 0);
  }

  GColor fill = grad_rgb_to_gcolor(fill_rgb);
  GColor sh2  = grad_rgb_to_gcolor(shad_rgb);
  GColor sh1  = grad_rgb_to_gcolor(grad_lerp(shad_rgb, fill_rgb, 128));

  GFont time_font = fonts_get_system_font(FONT_TIME);
  GFont date_font = s_date_font;
  
  /* NEU: Fallback, falls der Custom Font NULL ist */
  if (!date_font) {
      date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  }
  
  /* NEU: Datum an fester, modellabhaengiger Position (DATE_Y) */
  if (compose_date_line(now, now_min, date_font, b)) {
    GRect dr = GRect(0, DATE_Y, b.size.w, DATE_BOX_H + 4);
    draw_outlined_text(ctx, s_date_buf, date_font, dr, fill, sh1, sh2);
  }

  /* NEU: Zeit oben (TIME_TOP_Y) oder unten (TIME_BOTTOM_Y), je Modell */
  int time_y = s_time_at_top ? TIME_TOP_Y : TIME_BOTTOM_Y;
  format_time_string(now);
  GRect tr = GRect(0, time_y, b.size.w, TIME_BOX_H + 8);
  draw_outlined_text(ctx, s_time_buf, time_font, tr, fill, sh1, sh2);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int16_t W = b.size.w, H = b.size.h;
  int16_t baseline = H - H * GROUND_PCT / 100;
  int16_t sky_top  = H * ARC_APEX_TOP_PCT / 100;
  int16_t cx = W / 2;
  int16_t A_sun  = W / 2 - SUN_ARC_EDGE_PX;
  int16_t A_moon = W / 2 - MOON_ARC_EDGE_PX;

  /* 1) Hintergrund schwarz */
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  /* 2) Himmelsverlauf bis zum Horizont */
  time_t now = effective_now();
  struct tm *lt = localtime(&now);
  int now_min = lt->tm_hour * 60 + lt->tm_min;

  GradRGB top, hor;
  if (s_d.have) {
    sky_colors(now_min, &top, &hor);
  } else if (lt->tm_hour >= 7 && lt->tm_hour < 19) {
    top = DAY_TOP; hor = DAY_HOR;
  } else {
    top = NIGHT_TOP; hor = NIGHT_HOR;
  }
  wx_tint_sky(now, &top, &hor);
  gradient_fill_vertical_rgb(ctx, GRect(0, 0, W, baseline), top, hor);

  /* Wolken liegen immer HINTER Sonne und Mond - die bleiben sichtbar */
  draw_forecast_clouds(ctx, now_min, now, H, baseline, sky_top, cx, A_sun);
  draw_hour_strip(ctx, now_min, now, H, cx);

  if (!s_ahead_of_time) {
    /* Standard: Sonne/Mond hinter Bodensilhouette und Uhr */
    draw_bodies(ctx, now_min, now, H, baseline, sky_top, cx, A_sun, A_moon);
    draw_ground(ctx, b, now_min);
    draw_clock(ctx, b, now, now_min);
  } else {
    /* Vordergrund-Modus: Sonne/Mond vor Zeit/Datum, aber hinter Silhouette */
    draw_clock(ctx, b, now, now_min);
    draw_bodies(ctx, now_min, now, H, baseline, sky_top, cx, A_sun, A_moon);
    draw_ground(ctx, b, now_min);
  }
}

/* ======================= AppMessage / Persist ========================== */

enum {
  PK_HAVE = 1, PK_LAT, PK_SDECL, PK_SHA, PK_MDECL, PK_MHA, PK_MSHOW, PK_RECV,
  PK_ASTRO_DAWN, PK_NAUT_DAWN, PK_CIV_DAWN, PK_SUNRISE, PK_GOLD_RISE,
  PK_GOLD_SET, PK_SUNSET, PK_CIV_DUSK, PK_NAUT_DUSK, PK_ASTRO_DUSK,
  PK_MOON_RISE, PK_MOON_SET, PK_AUTO_TEXT, PK_SHOW_DATE, PK_AHEAD_OF_TIME,
  PK_TIME_AT_TOP, PK_TURBINE_ANIM, PK_HIGH_CONTRAST,   /* NEU */
  PK_RECV_DATE,                /* Kalendertag des letzten Telefon-Updates (yyyymmdd) */
  PK_MOON_ILLUM, PK_MOON_WAXING, PK_MOON_PHASE,        /* NEU: ans Ende, Werte stabil */
  PK_MORE_SAT,                 /* NEU: kraeftigere Himmelsfarben */
  PK_WX_MODE, PK_WX_SHOW_TEMP, PK_WX_DATA   /* Wetter */
};

static void save_data(void) {
  persist_write_bool(PK_HAVE, s_d.have);
  persist_write_int(PK_LAT,   s_d.lat_mdeg);
  persist_write_int(PK_SDECL, s_d.sun_decl_mdeg);
  persist_write_int(PK_SHA,   s_d.sun_ha_mdeg);
  persist_write_int(PK_MDECL, s_d.moon_decl_mdeg);
  persist_write_int(PK_MHA,   s_d.moon_ha_mdeg);
  persist_write_bool(PK_MSHOW, s_d.moon_show);
  persist_write_int(PK_RECV,  (int32_t)s_d.recv_epoch);
  persist_write_int(PK_ASTRO_DAWN, s_d.astro_dawn);
  persist_write_int(PK_NAUT_DAWN,  s_d.naut_dawn);
  persist_write_int(PK_CIV_DAWN,   s_d.civ_dawn);
  persist_write_int(PK_SUNRISE,    s_d.sunrise);
  persist_write_int(PK_GOLD_RISE,  s_d.golden_rise);
  persist_write_int(PK_GOLD_SET,   s_d.golden_set);
  persist_write_int(PK_SUNSET,     s_d.sunset);
  persist_write_int(PK_CIV_DUSK,   s_d.civ_dusk);
  persist_write_int(PK_NAUT_DUSK,  s_d.naut_dusk);
  persist_write_int(PK_ASTRO_DUSK, s_d.astro_dusk);
  persist_write_int(PK_MOON_RISE, s_d.moon_rise);
  persist_write_int(PK_MOON_SET,  s_d.moon_set);
  persist_write_int(PK_MOON_ILLUM,  s_d.moon_illum);    /* NEU */
  persist_write_bool(PK_MOON_WAXING, s_d.moon_waxing);  /* NEU */
}

static void save_settings(void) {
  persist_write_bool(PK_AUTO_TEXT, s_auto_text_color);
  persist_write_bool(PK_SHOW_DATE, s_show_date);
  persist_write_bool(PK_AHEAD_OF_TIME, s_ahead_of_time);
  persist_write_bool(PK_TIME_AT_TOP, s_time_at_top);  /* NEU */
  persist_write_bool(PK_TURBINE_ANIM, s_turbine_anim);        /* NEU */
  persist_write_bool(PK_HIGH_CONTRAST, s_high_contrast);      /* NEU */
  persist_write_bool(PK_MOON_PHASE, s_show_moon_phase);       /* NEU */
  persist_write_bool(PK_MORE_SAT, s_more_sat);                /* NEU */
  persist_write_int(PK_WX_MODE, s_wx_mode);
  persist_write_bool(PK_WX_SHOW_TEMP, s_wx_show_temp);
}

static void load_settings(void) {
  s_auto_text_color = persist_exists(PK_AUTO_TEXT) ? persist_read_bool(PK_AUTO_TEXT) : false;
  s_show_date       = persist_exists(PK_SHOW_DATE)  ? persist_read_bool(PK_SHOW_DATE)  : false;
  s_ahead_of_time   = persist_exists(PK_AHEAD_OF_TIME) ? persist_read_bool(PK_AHEAD_OF_TIME) : false;
  s_time_at_top = persist_exists(PK_TIME_AT_TOP) ? persist_read_bool(PK_TIME_AT_TOP) : false; /* NEU */
  s_turbine_anim    = persist_exists(PK_TURBINE_ANIM) ? persist_read_bool(PK_TURBINE_ANIM) : false;       /* NEU */
  s_high_contrast   = persist_exists(PK_HIGH_CONTRAST) ? persist_read_bool(PK_HIGH_CONTRAST) : false;     /* NEU */
  s_show_moon_phase = persist_exists(PK_MOON_PHASE) ? persist_read_bool(PK_MOON_PHASE) : false;           /* NEU */
  s_more_sat        = persist_exists(PK_MORE_SAT)   ? persist_read_bool(PK_MORE_SAT)   : false;           /* NEU */
  s_wx_mode         = persist_exists(PK_WX_MODE)    ? persist_read_int(PK_WX_MODE)     : WX_MODE_OFF;
  s_wx_show_temp    = persist_exists(PK_WX_SHOW_TEMP) ? persist_read_bool(PK_WX_SHOW_TEMP) : false;
  memset(&s_wx, 0, sizeof(s_wx));
  if (persist_get_size(PK_WX_DATA) == (int)sizeof(s_wx)) {
    persist_read_data(PK_WX_DATA, &s_wx, sizeof(s_wx));
  }
}

static int16_t read_twilight_key(uint32_t key) {
  if (!persist_exists(key)) return -1;
  int32_t v = persist_read_int(key);
  return (v > 0) ? (int16_t)v : -1;
}

static void twilight_defaults(void) {
  s_d.astro_dawn  = -1;
  s_d.naut_dawn   = -1;
  s_d.civ_dawn    = -1;
  s_d.sunrise     = -1;
  s_d.golden_rise = -1;
  s_d.golden_set  = -1;
  s_d.sunset      = -1;
  s_d.civ_dusk    = -1;
  s_d.naut_dusk   = -1;
  s_d.astro_dusk  = -1;
  s_d.moon_rise   = -1;
  s_d.moon_set    = -1;
}

static void load_data(void) {
  twilight_defaults();
  if (!persist_exists(PK_HAVE)) { s_d.have = false; return; }
  s_d.have          = persist_read_bool(PK_HAVE);
  s_d.lat_mdeg      = persist_read_int(PK_LAT);
  s_d.sun_decl_mdeg = persist_read_int(PK_SDECL);
  s_d.sun_ha_mdeg   = persist_read_int(PK_SHA);
  s_d.moon_decl_mdeg= persist_read_int(PK_MDECL);
  s_d.moon_ha_mdeg  = persist_read_int(PK_MHA);
  s_d.moon_show     = persist_read_bool(PK_MSHOW);
  s_d.recv_epoch    = (time_t)persist_read_int(PK_RECV);
  s_d.astro_dawn  = read_twilight_key(PK_ASTRO_DAWN);
  s_d.naut_dawn   = read_twilight_key(PK_NAUT_DAWN);
  s_d.civ_dawn    = read_twilight_key(PK_CIV_DAWN);
  s_d.sunrise     = read_twilight_key(PK_SUNRISE);
  s_d.golden_rise = read_twilight_key(PK_GOLD_RISE);
  s_d.golden_set  = read_twilight_key(PK_GOLD_SET);
  s_d.sunset      = read_twilight_key(PK_SUNSET);
  s_d.civ_dusk    = read_twilight_key(PK_CIV_DUSK);
  s_d.naut_dusk   = read_twilight_key(PK_NAUT_DUSK);
  s_d.astro_dusk  = read_twilight_key(PK_ASTRO_DUSK);
  s_d.moon_rise   = read_twilight_key(PK_MOON_RISE);
  s_d.moon_set    = read_twilight_key(PK_MOON_SET);
  s_d.moon_illum  = persist_exists(PK_MOON_ILLUM)  ? persist_read_int(PK_MOON_ILLUM) : 0;     /* NEU */
  s_d.moon_waxing = persist_exists(PK_MOON_WAXING) ? persist_read_bool(PK_MOON_WAXING) : true; /* NEU */
}

static int16_t tup_i16(DictionaryIterator *it, uint32_t key, int16_t def) {
  Tuple *t = dict_find(it, key);
  return t ? (int16_t)t->value->int32 : def;
}
static int32_t tup_i32(DictionaryIterator *it, uint32_t key, int32_t def) {
  Tuple *t = dict_find(it, key);
  return t ? (int32_t)t->value->int32 : def;
}

/* Zahl aus einem Tupel, egal ob Integer (eigene Nachricht) oder String
 * (Clay schickt Select-Werte als Text). */
static int32_t tup_any_int(Tuple *t) {
  if (t->type == TUPLE_CSTRING) return atoi(t->value->cstring);
  if (t->length == 1) return (t->type == TUPLE_INT) ? t->value->int8  : t->value->uint8;
  if (t->length == 2) return (t->type == TUPLE_INT) ? t->value->int16 : t->value->uint16;
  return t->value->int32;
}

static void wx_receive(DictionaryIterator *it) {
  Tuple *mode = dict_find(it, MESSAGE_KEY_WX_MODE);
  if (mode) {
    int32_t m = tup_any_int(mode);
    s_wx_mode = (m >= WX_MODE_OFF && m <= WX_MODE_STRIP) ? (int)m : WX_MODE_OFF;
  }
  Tuple *st = dict_find(it, MESSAGE_KEY_WX_SHOW_TEMP);
  if (st) s_wx_show_temp = (tup_any_int(st) != 0);

  Tuple *start = dict_find(it, MESSAGE_KEY_WX_START);
  Tuple *hours = dict_find(it, MESSAGE_KEY_WX_HOURS);
  Tuple *wind  = dict_find(it, MESSAGE_KEY_WX_WIND);
  Tuple *temps = dict_find(it, MESSAGE_KEY_WX_TEMPS);
  if (!start || !hours || !wind || !temps) return;
  if (hours->type != TUPLE_BYTE_ARRAY || hours->length != WX_HOURS) return;
  if (wind->type  != TUPLE_BYTE_ARRAY || wind->length  != WX_HOURS) return;
  if (temps->type != TUPLE_BYTE_ARRAY || temps->length != sizeof(s_wx.temps)) return;
  s_wx.version = WX_DATA_VERSION;
  s_wx.start   = tup_any_int(start);
  s_wx.recv    = (int32_t)time(NULL);
  memcpy(s_wx.hours, hours->value->data, WX_HOURS);
  memcpy(s_wx.wind,  wind->value->data,  WX_HOURS);
  memcpy(s_wx.temps, temps->value->data, sizeof(s_wx.temps));
  persist_write_data(PK_WX_DATA, &s_wx, sizeof(s_wx));
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  (void)ctx;
  wx_receive(it);
  /* Reines Wetterpaket: Astronomie-Daten nicht anfassen */
  if (dict_find(it, MESSAGE_KEY_WX_HOURS) && !dict_find(it, MESSAGE_KEY_SUN_DECL)) {
    save_settings();
    if (s_canvas) layer_mark_dirty(s_canvas);
    return;
  }
  s_d.lat_mdeg       = tup_i32(it, MESSAGE_KEY_LATITUDE,   s_d.lat_mdeg);
  s_d.sun_decl_mdeg  = tup_i32(it, MESSAGE_KEY_SUN_DECL,   s_d.sun_decl_mdeg);
  s_d.sun_ha_mdeg    = tup_i32(it, MESSAGE_KEY_SUN_HA,     s_d.sun_ha_mdeg);
  s_d.moon_decl_mdeg = tup_i32(it, MESSAGE_KEY_MOON_DECL,  s_d.moon_decl_mdeg);
  s_d.moon_ha_mdeg   = tup_i32(it, MESSAGE_KEY_MOON_HA,    s_d.moon_ha_mdeg);

  Tuple *ms = dict_find(it, MESSAGE_KEY_MOON_SHOW);
  if (ms) s_d.moon_show = (ms->value->int32 != 0);

  Tuple *at = dict_find(it, MESSAGE_KEY_AUTO_CYCLE_TEXT_COLOR);
  if (at) s_auto_text_color = (at->value->int32 != 0);
  Tuple *sd = dict_find(it, MESSAGE_KEY_SHOW_DATE);
  if (sd) s_show_date = (sd->value->int32 != 0);
  Tuple *ah = dict_find(it, MESSAGE_KEY_AHEAD_OF_TIME);
  if (ah) s_ahead_of_time = (ah->value->int32 != 0);

  Tuple *tad = dict_find(it, MESSAGE_KEY_TIME_AT_TOP);   /* NEU */
  if (tad) s_time_at_top = (tad->value->int32 != 0);

  Tuple *hc = dict_find(it, MESSAGE_KEY_HIGH_CONTRAST);  /* NEU */
  if (hc) s_high_contrast = (hc->value->int32 != 0);

  Tuple *mp = dict_find(it, MESSAGE_KEY_MOON_PHASE);     /* NEU */
  if (mp) s_show_moon_phase = (mp->value->int32 != 0);

  Tuple *msat = dict_find(it, MESSAGE_KEY_MORE_SAT);     /* NEU */
  if (msat) s_more_sat = (msat->value->int32 != 0);

  Tuple *mw = dict_find(it, MESSAGE_KEY_MOON_WAXING);    /* NEU */
  if (mw) s_d.moon_waxing = (mw->value->int32 != 0);

  Tuple *ta = dict_find(it, MESSAGE_KEY_TURBINE_ANIM);        /* NEU */
  if (ta) {
    bool new_anim = (ta->value->int32 != 0);
    if (!new_anim && s_turbine_timer) {     /* laufende Drehung sauber stoppen */
      app_timer_cancel(s_turbine_timer);
      s_turbine_timer = NULL;
    }
    s_turbine_anim = new_anim;
  }

  s_d.astro_dawn  = tup_i16(it, MESSAGE_KEY_ASTRO_DAWN_MIN,    s_d.astro_dawn);
  s_d.naut_dawn   = tup_i16(it, MESSAGE_KEY_NAUTICAL_DAWN_MIN, s_d.naut_dawn);
  s_d.civ_dawn    = tup_i16(it, MESSAGE_KEY_CIVIL_DAWN_MIN,    s_d.civ_dawn);
  s_d.sunrise     = tup_i16(it, MESSAGE_KEY_SUNRISE_MIN,       s_d.sunrise);
  s_d.golden_rise = tup_i16(it, MESSAGE_KEY_GOLDEN_RISE_MIN,   s_d.golden_rise);
  s_d.golden_set  = tup_i16(it, MESSAGE_KEY_GOLDEN_SET_MIN,    s_d.golden_set);
  s_d.sunset      = tup_i16(it, MESSAGE_KEY_SUNSET_MIN,        s_d.sunset);
  s_d.civ_dusk    = tup_i16(it, MESSAGE_KEY_CIVIL_DUSK_MIN,    s_d.civ_dusk);
  s_d.naut_dusk   = tup_i16(it, MESSAGE_KEY_NAUTICAL_DUSK_MIN, s_d.naut_dusk);
  s_d.astro_dusk  = tup_i16(it, MESSAGE_KEY_ASTRO_DUSK_MIN,    s_d.astro_dusk);
  s_d.moon_rise   = tup_i16(it, MESSAGE_KEY_MOON_RISE_MIN,    s_d.moon_rise);
  s_d.moon_set    = tup_i16(it, MESSAGE_KEY_MOON_SET_MIN,     s_d.moon_set);
  /* Mondphase: 0 ist gueltig (Neumond), daher NICHT in den -1-Block unten */
  s_d.moon_illum  = tup_i16(it, MESSAGE_KEY_MOON_ILLUM, s_d.moon_illum);
  if (s_d.moon_illum < 0)   s_d.moon_illum = 0;
  if (s_d.moon_illum > 100) s_d.moon_illum = 100;
  /* Ungueltige 0-Werte vom Telefon ignorieren */
  if (s_d.astro_dawn  <= 0) s_d.astro_dawn  = -1;
  if (s_d.naut_dawn   <= 0) s_d.naut_dawn   = -1;
  if (s_d.civ_dawn    <= 0) s_d.civ_dawn    = -1;
  if (s_d.sunrise     <= 0) s_d.sunrise     = -1;
  if (s_d.golden_rise <= 0) s_d.golden_rise = -1;
  if (s_d.golden_set  <= 0) s_d.golden_set  = -1;
  if (s_d.sunset      <= 0) s_d.sunset      = -1;
  if (s_d.civ_dusk    <= 0) s_d.civ_dusk    = -1;
  if (s_d.naut_dusk   <= 0) s_d.naut_dusk   = -1;
  if (s_d.astro_dusk  <= 0) s_d.astro_dusk  = -1;
  if (s_d.moon_rise   <= 0) s_d.moon_rise   = -1;
  if (s_d.moon_set    <= 0) s_d.moon_set    = -1;

  s_d.recv_epoch =
#ifdef DEBUG_TIMELAPSE
    s_sim_now;
#else
    time(NULL);
#endif
  s_d.have = true;
  save_data();
  save_settings();
  if (s_canvas) layer_mark_dirty(s_canvas);
}

static void request_update(void) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, MESSAGE_KEY_REQUEST_UPDATE, 1);
  /* Heutiges Datum als yyyymmdd mitsenden: Telefon prueft ob es heute
   * bereits gesendet hat und ueberspringt GPS/Berechnung wenn ja. */
  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  int32_t yyyymmdd = (lt->tm_year + 1900) * 10000
                   + (lt->tm_mon  + 1)    * 100
                   + lt->tm_mday;
  dict_write_int32(it, MESSAGE_KEY_REQUEST_DATE, yyyymmdd);
  app_message_outbox_send();
}

/* Wetter stuendlich nachfordern. Die Uhr fragt, das Telefon holt. */
static void maybe_request_weather(void) {
  if (s_wx_mode == WX_MODE_OFF) return;
  time_t now = time(NULL);
  if (s_wx.version == WX_DATA_VERSION && now - (time_t)s_wx.recv < WX_REFRESH_S) return;
  if (now - s_wx_last_req < WX_RETRY_S) return;
  if (!connection_service_peek_pebblekit_connection()) return;
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, MESSAGE_KEY_REQUEST_WEATHER, 1);
  app_message_outbox_send();
  s_wx_last_req = now;
}

/* ======================= Windrad-Animation (NEU) ====================== */

/* Ease-out (quadratisch): schneller Start, sanftes Auslaufen. t,dur in ms. */
static int32_t turbine_ease_out(int32_t from, int32_t to, uint32_t t, uint32_t dur) {
  if (dur == 0 || t >= dur) return to;
  int32_t x   = (int32_t)((int64_t)t * 1000 / dur);   /* 0..1000 */
  int32_t inv = 1000 - x;
  int32_t p   = 1000 - (int32_t)((int64_t)inv * inv / 1000); /* 0..1000 */
  return from + (int32_t)((int64_t)(to - from) * p / 1000);
}

static void turbine_anim_tick(void *data) {
  (void)data;
  s_turbine_elapsed_ms += TURBINE_ANIM_FRAME_MS;
  if (s_turbine_elapsed_ms >= TURBINE_ANIM_MS) {
    s_turbine_angle = s_turbine_to % TRIG_MAX_ANGLE;  /* Ruhewinkel = Ziel */
    s_turbine_timer = NULL;
    if (s_canvas) layer_mark_dirty(s_canvas);
    return;
  }
  s_turbine_angle = turbine_ease_out(s_turbine_from, s_turbine_to,
                                     s_turbine_elapsed_ms, TURBINE_ANIM_MS);
  if (s_canvas) layer_mark_dirty(s_canvas);
  s_turbine_timer = app_timer_register(TURBINE_ANIM_FRAME_MS, turbine_anim_tick, NULL);
}

/* Drehung je Minute: fest ohne Wetter, sonst nach Windstaerke (Flaute = 0) */
static int32_t turbine_step(void) {
  int kmh = wx_wind_now();
  int deg = TURBINE_STEP_DEG;
  if (kmh >= 0) {
    deg = (kmh < WX_CALM_KMH) ? 0 : kmh * WX_TURB_DEG_PER_KMH;
    if (deg > WX_TURB_MAX_STEP_DEG) deg = WX_TURB_MAX_STEP_DEG;
  }
  return (int32_t)TRIG_MAX_ANGLE * deg / 360;
}

static int turbine_turns(void) {
  int kmh = wx_wind_now();
  if (kmh < 0) return TURBINE_ANIM_TURNS;
  if (kmh < WX_CALM_KMH) return 0;
  int t = kmh / WX_KMH_PER_TURN;
  return (t > WX_TURB_MAX_TURNS) ? WX_TURB_MAX_TURNS : t;
}

static void turbine_anim_start(void) {
  int32_t step = turbine_step();
  int turns = turbine_turns();
  if (step == 0 && turns == 0) return;          /* Flaute: Windrad steht */
  s_turbine_from = s_turbine_angle;
  s_turbine_to   = s_turbine_angle
                 + (int32_t)TRIG_MAX_ANGLE * turns + step;
  s_turbine_elapsed_ms = 0;
  if (s_turbine_timer) app_timer_cancel(s_turbine_timer);
  s_turbine_timer = app_timer_register(TURBINE_ANIM_FRAME_MS, turbine_anim_tick, NULL);
}

/* NEU: Windrad ohne Animation einen Schritt weiterdrehen (harter Sprung).
 * Dreht jede Minute um TURBINE_STEP_DEG weiter, ohne Zwischenframes. Der
 * Canvas wird ohnehin im tick_handler einmal pro Minute neu gezeichnet. */
static void turbine_step_instant(void) {
  int32_t step = turbine_step();
  s_turbine_angle = (s_turbine_angle + step) % TRIG_MAX_ANGLE;
}

static void tick_handler(struct tm *t, TimeUnits units) {
#ifdef DEBUG_TIMELAPSE
  (void)t;
  debug_timelapse_advance();
#else
  /* Tageswechsel: frische Daten anfordern. Das Telefon rechnet nur wenn
   * es fuer diesen Kalendertag noch nicht gesendet hat. */
  if ((units & DAY_UNIT) || (t->tm_hour == 0 && t->tm_min == 0)) {
    request_update();
  }
#endif
  /* Windrad pro Minute weiterdrehen: weich (Animation an) oder als harter
   * Sprung um TURBINE_STEP_DEG (Animation aus). */
  if (units & MINUTE_UNIT) {
    maybe_request_weather();
    if (s_turbine_anim) {
      turbine_anim_start();        /* weiche Drehung ueber mehrere Frames */
    } else {
      turbine_step_instant();      /* NEU: ein Schritt pro Minute, ohne Animation */
    }
  }
  if (s_canvas) layer_mark_dirty(s_canvas);
}

/* ======================= Fenster / Init ================================ */

static void window_load(Window *win) {
  s_date_font = fonts_get_system_font(FONT_DATE);
  Layer *root = window_get_root_layer(win);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *win) {
  (void)win;
  /* s_date_font stammt aus fonts_get_system_font(): ein Systemfont darf NICHT
   * mit fonts_unload_custom_font entladen werden. Der fruehere Aufruf
   * beschaedigte beim Fenster-Entladen die Font-Verwaltung und fuehrte beim
   * naechsten Rendern zum App fault. Zeile entfernt. */
  layer_destroy(s_canvas);
}

static void init(void) {
  srand((unsigned)time(NULL));                 /* NEU: fuer Seitenabstand-Zufall */
  s_d.have = false;
  twilight_defaults();
  load_data();
  load_settings();

  /* NEU: Abstand Baum/Haus einmal beim Start leicht zufaellig (+/- Jitter) */
  int span = 2 * SIDE_ELEM_JITTER_PX + 1;
  s_tree_gap  = (int16_t)(SIDE_ELEM_GAP_PX + (rand() % span) - SIDE_ELEM_JITTER_PX);
  s_house_gap = (int16_t)(SIDE_ELEM_GAP_PX + (rand() % span) - SIDE_ELEM_JITTER_PX);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received);
  /* Inbox muss das groesste Telefon-Paket fassen: calcAndSend sendet ~24 Keys
   * (je ~11 Byte) = ~270 Byte. 256 war zu knapp, 512 gibt Reserve. */
  app_message_open(512, 256);
#ifdef DEBUG_TIMELAPSE
  debug_timelapse_init();
  tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
#else
  tick_timer_service_subscribe(MINUTE_UNIT | DAY_UNIT, tick_handler);
#endif
  request_update();
  s_wx_last_req = time(NULL);    /* das Telefon holt beim Start ohnehin Wetter */
}

static void deinit(void) {
  if (s_turbine_timer) { app_timer_cancel(s_turbine_timer); s_turbine_timer = NULL; }  /* NEU */
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}