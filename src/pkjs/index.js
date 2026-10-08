// Import the Clay package
var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);
var Keys = require('message_keys');

// moon.js wird fuer dieses Watchface NICHT mehr benoetigt - Mondposition
// (Deklination + Stundenwinkel) wird hier kompakt selbst gerechnet.

// === Fallback location (Elmshorn, Germany) ===
var FALLBACK_LAT = 53.75;
var FALLBACK_LON =  9.65;

// ─────────────────────────────────────────────
//  Gradzahl-Trigonometrie-Helfer
// ─────────────────────────────────────────────
var D2R = Math.PI / 180.0, R2D = 180.0 / Math.PI;
function sind(x) { return Math.sin(x * D2R); }
function cosd(x) { return Math.cos(x * D2R); }
function atan2d(y, x) { return Math.atan2(y, x) * R2D; }
function rev(x) { x = x % 360; return x < 0 ? x + 360 : x; }
function norm180(x) { x = rev(x); return x > 180 ? x - 360 : x; }

function getStoredSettings() {
  try { return JSON.parse(localStorage.getItem('clay-settings') || '{}'); }
  catch (e) { console.log('[solar] settings: ' + e); return {}; }
}

// ─────────────────────────────────────────────
//  Sendewarteschlange: AppMessage transportiert nur eine Nachricht
//  gleichzeitig. Astronomie, Settings und Wetter laufen sonst ineinander.
// ─────────────────────────────────────────────
var sendQueue = [], sending = false;

function sendMsg(msg, label, cb) {
  sendQueue.push({ msg: msg, label: label, cb: cb });
  if (!sending) pumpQueue();
}

function pumpQueue() {
  var job = sendQueue.shift();
  if (!job) { sending = false; return; }
  sending = true;
  Pebble.sendAppMessage(job.msg,
    function()  { console.log('[solar] ' + job.label + ' sent OK'); if (job.cb) job.cb(); pumpQueue(); },
    function(e) { console.log('[solar] ' + job.label + ' send failed: ' + JSON.stringify(e)); if (job.cb) job.cb(); pumpQueue(); });
}

// Heutiges Datum als yyyymmdd-Zahl (lokale Zeit)
function todayInt() {
  var n = new Date();
  return n.getFullYear() * 10000 + (n.getMonth() + 1) * 100 + n.getDate();
}

// ─────────────────────────────────────────────
//  NOAA Sunrise / Sunset (Daemmerungs-Schwellen)
// ─────────────────────────────────────────────
function toJulianDate(date) {
  var Y = date.getUTCFullYear(), M = date.getUTCMonth() + 1, D = date.getUTCDate();
  if (M <= 2) { Y -= 1; M += 12; }
  var A = Math.floor(Y / 100), B = 2 - A + Math.floor(A / 4);
  return Math.floor(365.25 * (Y + 4716)) + Math.floor(30.6001 * (M + 1)) + D + B - 1524.5;
}

// Liefert {rise,set} in UTC-Minuten, oder null bei Polartag/-nacht.
function calcRiseSetForDecl(JD, lat, lon, zenith, declDeg) {
  var T  = (JD - 2451545.0) / 36525.0;
  var L0 = (280.46646 + T * (36000.76983 + T * 0.0003032)) % 360.0;
  var M  = 357.52911 + T * (35999.05029 - T * 0.0001537);
  var Mr = M * D2R;
  var e = 0.016708634, L0r = L0 * D2R;
  var eps = (23.439291111 - T * (0.013004167 + T * (0.000000164 - T * 0.000000504))) * D2R;
  var y = Math.pow(Math.tan(eps / 2.0), 2);
  var eot = 4.0 * R2D * (
      y * Math.sin(2 * L0r) - 2 * e * Math.sin(Mr)
    + 4 * e * y * Math.sin(Mr) * Math.cos(2 * L0r)
    - 0.5 * y * y * Math.sin(4 * L0r) - 1.25 * e * e * Math.sin(2 * Mr));
  var dec = declDeg * D2R;
  var latR = lat * D2R;
  var cosHA = (cosd(zenith) - Math.sin(latR) * Math.sin(dec)) / (Math.cos(latR) * Math.cos(dec));
  if (cosHA > 1.0 || cosHA < -1.0) return null;
  var HA = Math.acos(cosHA) * R2D, solarNoon = 720.0 - 4.0 * lon - eot;
  return { rise: solarNoon - HA * 4.0, set: solarNoon + HA * 4.0 };
}

function calcSunEvent(JD, lat, lon, zenith) {
  var T  = (JD - 2451545.0) / 36525.0;
  var L0 = (280.46646 + T * (36000.76983 + T * 0.0003032)) % 360.0;
  var M  = 357.52911 + T * (35999.05029 - T * 0.0001537);
  var Mr = M * D2R;
  var C  = Math.sin(Mr) * (1.914602 - T * (0.004817 + 0.000014 * T))
         + Math.sin(2 * Mr) * (0.019993 - T * 0.000101)
         + Math.sin(3 * Mr) * 0.000289;
  var om  = 125.04 - 1934.136 * T;
  var lam = (L0 + C - 0.00569 - 0.00478 * sind(om)) * D2R;
  var eps = (23.439291111 - T * (0.013004167 + T * (0.000000164 - T * 0.000000504))) * D2R;
  var dec = Math.asin(Math.sin(eps) * Math.sin(lam)) * R2D;
  return calcRiseSetForDecl(JD, lat, lon, zenith, dec);
}

function moonAltDeg(t, lat, lon) {
  var m = moonDeclHA(t, lon);
  var latR = lat * D2R, decR = m.declDeg * D2R, haR = m.haDeg * D2R;
  var s = Math.sin(latR) * Math.sin(decR) + Math.cos(latR) * Math.cos(decR) * Math.cos(haR);
  if (s > 1) s = 1; if (s < -1) s = -1;
  return Math.asin(s) * R2D;
}

/* Mond-Auf-/Untergang heute (lokale Min.), Fenster das now enthaelt */
function moonRiseSetForNow(now, lat, lon) {
  var base = new Date(now);
  base.setHours(0, 0, 0, 0);
  var nowMin = now.getHours() * 60 + now.getMinutes();
  var prev = moonAltDeg(base, lat, lon);
  var lastRise = -1, best = { rise: -1, set: -1 };
  for (var m = 1; m <= 1440; m++) {
    var alt = moonAltDeg(new Date(base.getTime() + m * 60000), lat, lon);
    if (prev < 0 && alt >= 0) lastRise = m;
    if (prev >= 0 && alt < 0 && lastRise >= 0) {
      if (nowMin >= lastRise && nowMin <= m) return { rise: lastRise, set: m };
      if (lastRise <= m) best = { rise: lastRise, set: m };
      lastRise = -1;
    }
    prev = alt;
  }
  if (lastRise >= 0 && nowMin >= lastRise) return { rise: lastRise, set: 1439 };
  return best;
}

function toLocalMin(utcMin, tzOff) {
  var m = Math.round(utcMin - tzOff);
  if (m < 0) m += 1440; if (m >= 1440) m -= 1440;
  return m;
}

// ─────────────────────────────────────────────
//  Sonnen-Deklination + aktueller Stundenwinkel
// ─────────────────────────────────────────────
function sunDeclHA(now, lon) {
  var JD = toJulianDate(now);
  var T  = (JD - 2451545.0) / 36525.0;
  var L0 = rev(280.46646 + T * (36000.76983 + T * 0.0003032));
  var M  = 357.52911 + T * (35999.05029 - T * 0.0001537), Mr = M * D2R;
  var C  = Math.sin(Mr) * (1.914602 - T * (0.004817 + 0.000014 * T))
         + Math.sin(2 * Mr) * (0.019993 - T * 0.000101)
         + Math.sin(3 * Mr) * 0.000289;
  var om  = 125.04 - 1934.136 * T;
  var lam = (L0 + C - 0.00569 - 0.00478 * sind(om)) * D2R;
  var eps = (23.439291111 - T * (0.013004167 + T * 0.000000164)) * D2R;
  var dec = Math.asin(Math.sin(eps) * Math.sin(lam)) * R2D;
  var e = 0.016708634, L0r = L0 * D2R, y = Math.pow(Math.tan(eps / 2.0), 2);
  var eot = 4.0 * R2D * (
      y * Math.sin(2 * L0r) - 2 * e * Math.sin(Mr)
    + 4 * e * y * Math.sin(Mr) * Math.cos(2 * L0r)
    - 0.5 * y * y * Math.sin(4 * L0r) - 1.25 * e * e * Math.sin(2 * Mr));
  var solarNoon = 720.0 - 4.0 * lon - eot;                  // UTC-Minuten
  var nowUTCmin = now.getUTCHours() * 60 + now.getUTCMinutes() + now.getUTCSeconds() / 60;
  var haDeg = norm180((nowUTCmin - solarNoon) * 0.25);      // 0.25 Grad/min
  return { declDeg: dec, haDeg: haDeg };
}

// ─────────────────────────────────────────────
//  Mond: Deklination + Stundenwinkel (Schlyter, ~0.1 Grad)
// ─────────────────────────────────────────────
function moonDeclHA(now, lon) {
  var Y = now.getUTCFullYear(), Mo = now.getUTCMonth() + 1, Da = now.getUTCDate();
  var UT = now.getUTCHours() + now.getUTCMinutes() / 60 + now.getUTCSeconds() / 3600;
  var d = 367 * Y - Math.floor(7 * (Y + Math.floor((Mo + 9) / 12)) / 4)
        + Math.floor(275 * Mo / 9) + Da - 730530 + UT / 24;

  // Sonne (fuer Stoerterme + Sternzeit)
  var ws = 282.9404 + 4.70935e-5 * d;
  var Ms = rev(356.0470 + 0.9856002585 * d);
  var Ls = rev(ws + Ms);

  // Mond-Bahnelemente
  var N = rev(125.1228 - 0.0529538083 * d);
  var i = 5.1454;
  var w = rev(318.0634 + 0.1643573223 * d);
  var a = 60.2666, e = 0.054900;
  var Mm = rev(115.3654 + 13.0649929509 * d);

  // Kepler
  var E = Mm + R2D * e * sind(Mm) * (1 + e * cosd(Mm));
  for (var k = 0; k < 5; k++) {
    E = E - (E - R2D * e * sind(E) - Mm) / (1 - e * cosd(E));
  }
  var xv = a * (cosd(E) - e), yv = a * Math.sqrt(1 - e * e) * sind(E);
  var v = rev(atan2d(yv, xv)), r = Math.sqrt(xv * xv + yv * yv);

  // Ekliptikale Rechteckkoordinaten
  var xh = r * (cosd(N) * cosd(v + w) - sind(N) * sind(v + w) * cosd(i));
  var yh = r * (sind(N) * cosd(v + w) + cosd(N) * sind(v + w) * cosd(i));
  var zh = r * (sind(v + w) * sind(i));
  var lonecl = rev(atan2d(yh, xh));
  var latecl = atan2d(zh, Math.sqrt(xh * xh + yh * yh));

  // Stoerterme
  var Lm = rev(N + w + Mm);
  var Dm = rev(Lm - Ls);
  var F  = rev(Lm - N);
  lonecl += -1.274 * sind(Mm - 2 * Dm) + 0.658 * sind(2 * Dm)
          -  0.186 * sind(Ms)          - 0.059 * sind(2 * Mm - 2 * Dm)
          -  0.057 * sind(Mm - 2 * Dm + Ms) + 0.053 * sind(Mm + 2 * Dm)
          +  0.046 * sind(2 * Dm - Ms) + 0.041 * sind(Mm - Ms)
          -  0.035 * sind(Dm)          - 0.031 * sind(Mm + Ms)
          -  0.015 * sind(2 * F - 2 * Dm) + 0.011 * sind(Mm - 4 * Dm);
  latecl += -0.173 * sind(F - 2 * Dm)  - 0.055 * sind(Mm - F - 2 * Dm)
          -  0.046 * sind(Mm + F - 2 * Dm) + 0.033 * sind(F + 2 * Dm)
          +  0.017 * sind(2 * Mm + F);

  // Ekliptik -> Aequator
  var obl = 23.4393 - 3.563e-7 * d;
  var xg = cosd(lonecl) * cosd(latecl);
  var yg = sind(lonecl) * cosd(latecl);
  var zg = sind(latecl);
  var xe = xg;
  var ye = yg * cosd(obl) - zg * sind(obl);
  var ze = yg * sind(obl) + zg * cosd(obl);
  var RA  = rev(atan2d(ye, xe));
  var Dec = atan2d(ze, Math.sqrt(xe * xe + ye * ye));

  // Sternzeit -> Stundenwinkel
  var GMST0 = rev(Ls + 180);
  var GMST  = GMST0 + UT * 15.0;
  var LST   = GMST + lon;
  var haDeg = norm180(LST - RA);
  return { declDeg: Dec, haDeg: haDeg };
}

// ─────────────────────────────────────────────
//  Settings-Paket: nur Toggles, kein GPS
//  Wird bei jedem Clay-Close sofort gesendet.
// ─────────────────────────────────────────────
function sendSettings(cb) {
  var settings = getStoredSettings();
  var msg = {};
  msg[Keys.AUTO_CYCLE_TEXT_COLOR] =
    (settings.AUTO_CYCLE_TEXT_COLOR !== undefined && settings.AUTO_CYCLE_TEXT_COLOR) ? 1 : 0;
  msg[Keys.SHOW_DATE] =
    (settings.SHOW_DATE !== undefined && settings.SHOW_DATE) ? 1 : 0;
  msg[Keys.TIME_AT_TOP] =
    (settings.TIME_AT_TOP !== undefined && settings.TIME_AT_TOP) ? 1 : 0;
  msg[Keys.AHEAD_OF_TIME] =
    (settings.AHEAD_OF_TIME !== undefined && settings.AHEAD_OF_TIME) ? 1 : 0;
  msg[Keys.TURBINE_ANIM] =
    (settings.TURBINE_ANIM !== undefined && settings.TURBINE_ANIM) ? 1 : 0;
  msg[Keys.HIGH_CONTRAST] =
    (settings.HIGH_CONTRAST !== undefined && settings.HIGH_CONTRAST) ? 1 : 0;
  msg[Keys.MOON_SHOW] =
    (settings.MOON_SHOW === undefined || settings.MOON_SHOW) ? 1 : 0;
  msg[Keys.MOON_PHASE] =
    (settings.MOON_PHASE !== undefined && settings.MOON_PHASE) ? 1 : 0;
  msg[Keys.MORE_SAT] =
    (settings.MORE_SAT !== undefined && settings.MORE_SAT) ? 1 : 0;
  addWxSettings(msg, settings);
  console.log('[solar] sendSettings');
  sendMsg(msg, 'settings', cb);
}

// ─────────────────────────────────────────────
//  Astronomie-Paket: GPS + Berechnung + Senden
// ─────────────────────────────────────────────
// ─────────────────────────────────────────────
//  Mondphase: beleuchteter Anteil (0..100) + zunehmend/abnehmend
//  nach Paul Schlyter, ortsunabhaengig (Datum/Zeit genuegt).
// ─────────────────────────────────────────────
function moonIllum(now) {
  var Y = now.getUTCFullYear(), Mo = now.getUTCMonth() + 1, Da = now.getUTCDate();
  var UT = now.getUTCHours() + now.getUTCMinutes() / 60 + now.getUTCSeconds() / 3600;
  var d = 367 * Y - Math.floor(7 * (Y + Math.floor((Mo + 9) / 12)) / 4)
        + Math.floor(275 * Mo / 9) + Da - 730530 + UT / 24;

  // Sonne: wahre ekliptikale Laenge
  var ws = 282.9404 + 4.70935e-5 * d;
  var es = 0.016709 - 1.151e-9 * d;
  var Ms = rev(356.0470 + 0.9856002585 * d);
  var Es = Ms + es * R2D * sind(Ms) * (1 + es * cosd(Ms));
  var slon = rev(atan2d(sind(Es) * Math.sqrt(1 - es * es), cosd(Es) - es) + ws);

  // Mond: ekliptikale Laenge (Hauptstoerterme genuegen fuer den Anteil)
  var N = rev(125.1228 - 0.0529538083 * d);
  var im = 5.1454;
  var wm = rev(318.0634 + 0.1643573223 * d);
  var em = 0.054900;
  var Mm = rev(115.3654 + 13.0649929509 * d);
  var Em = Mm + em * R2D * sind(Mm) * (1 + em * cosd(Mm));
  for (var k = 0; k < 5; k++) {
    Em = Em - (Em - em * R2D * sind(Em) - Mm) / (1 - em * cosd(Em));
  }
  var vm = atan2d(Math.sqrt(1 - em * em) * sind(Em), cosd(Em) - em);
  var xh = cosd(N) * cosd(vm + wm) - sind(N) * sind(vm + wm) * cosd(im);
  var yh = sind(N) * cosd(vm + wm) + cosd(N) * sind(vm + wm) * cosd(im);
  var zh = sind(vm + wm) * sind(im);
  var mlon = rev(atan2d(yh, xh));
  var mlat = atan2d(zh, Math.sqrt(xh * xh + yh * yh));
  var Lm = rev(N + wm + Mm), Ls = rev(ws + Ms), Dm = rev(Lm - Ls);
  mlon = rev(mlon - 1.274 * sind(Mm - 2 * Dm) + 0.658 * sind(2 * Dm) - 0.186 * sind(Ms));

  var elong = Math.acos(cosd(slon - mlon) * cosd(mlat)) * R2D;   // 0..180
  var illum = Math.round((1 - cosd(elong)) / 2 * 100);
  if (illum < 0)   illum = 0;
  if (illum > 100) illum = 100;
  var ageDeg = rev(mlon - slon);          // 0 = Neumond, 180 = Vollmond
  return { illum: illum, waxing: (ageDeg < 180) ? 1 : 0 };
}

function calcAndSend(lat, lon, forceWx) {
  var now = new Date(), JD = toJulianDate(now), tzOff = now.getTimezoneOffset();
  function ev(z) { return calcSunEvent(JD, lat, lon, z); }
  var sr = ev(90.833), civ = ev(96.0), nau = ev(102.0), ast = ev(108.0), gold = ev(84.0);

  var sun  = sunDeclHA(now, lon);
  var moon = moonDeclHA(now, lon);
  var moonRs = moonRiseSetForNow(now, lat, lon);
  var settings = getStoredSettings();
  var showMoon = (settings.MOON_SHOW === undefined) ? true : !!settings.MOON_SHOW;

  var msg = {};
  msg[Keys.LATITUDE]  = Math.round(lat * 1000);
  msg[Keys.SUN_DECL]  = Math.round(sun.declDeg  * 1000);
  msg[Keys.SUN_HA]    = Math.round(sun.haDeg    * 1000);
  msg[Keys.MOON_DECL] = Math.round(moon.declDeg * 1000);
  msg[Keys.MOON_HA]   = Math.round(moon.haDeg   * 1000);
  msg[Keys.MOON_SHOW] = showMoon ? 1 : 0;
  msg[Keys.MOON_RISE_MIN] = moonRs.rise;
  msg[Keys.MOON_SET_MIN]  = moonRs.set;
  var mphase = moonIllum(now);
  msg[Keys.MOON_ILLUM]  = mphase.illum;
  msg[Keys.MOON_WAXING] = mphase.waxing;
  msg[Keys.MOON_PHASE]  =
    (settings.MOON_PHASE !== undefined && settings.MOON_PHASE) ? 1 : 0;
  msg[Keys.AUTO_CYCLE_TEXT_COLOR] =
    (settings.AUTO_CYCLE_TEXT_COLOR !== undefined && settings.AUTO_CYCLE_TEXT_COLOR) ? 1 : 0;
  msg[Keys.SHOW_DATE] =
    (settings.SHOW_DATE !== undefined && settings.SHOW_DATE) ? 1 : 0;
  msg[Keys.TIME_AT_TOP] =
    (settings.TIME_AT_TOP !== undefined && settings.TIME_AT_TOP) ? 1 : 0;
  msg[Keys.AHEAD_OF_TIME] =
    (settings.AHEAD_OF_TIME !== undefined && settings.AHEAD_OF_TIME) ? 1 : 0;
  msg[Keys.TURBINE_ANIM] =
    (settings.TURBINE_ANIM !== undefined && settings.TURBINE_ANIM) ? 1 : 0;
  msg[Keys.HIGH_CONTRAST] =
    (settings.HIGH_CONTRAST !== undefined && settings.HIGH_CONTRAST) ? 1 : 0;
  msg[Keys.MORE_SAT] =
    (settings.MORE_SAT !== undefined && settings.MORE_SAT) ? 1 : 0;
  addWxSettings(msg, settings);

  msg[Keys.SUNRISE_MIN]       = sr  ? toLocalMin(sr.rise,  tzOff) : -1;
  msg[Keys.SUNSET_MIN]        = sr  ? toLocalMin(sr.set,   tzOff) : -1;
  msg[Keys.CIVIL_DAWN_MIN]    = civ ? toLocalMin(civ.rise, tzOff) : -1;
  msg[Keys.CIVIL_DUSK_MIN]    = civ ? toLocalMin(civ.set,  tzOff) : -1;
  msg[Keys.NAUTICAL_DAWN_MIN] = nau ? toLocalMin(nau.rise, tzOff) : -1;
  msg[Keys.NAUTICAL_DUSK_MIN] = nau ? toLocalMin(nau.set,  tzOff) : -1;
  msg[Keys.ASTRO_DAWN_MIN]    = ast ? toLocalMin(ast.rise, tzOff) : -1;
  msg[Keys.ASTRO_DUSK_MIN]    = ast ? toLocalMin(ast.set,  tzOff) : -1;
  msg[Keys.GOLDEN_RISE_MIN]   = gold ? toLocalMin(gold.rise, tzOff) : -1;
  msg[Keys.GOLDEN_SET_MIN]    = gold ? toLocalMin(gold.set,  tzOff) : -1;

  /* Goldene Stunde: Aufgang 6 Grad nach Sonnenaufgang, Untergang 6 Grad vor Sonnenuntergang */
  if (sr && gold) {
    var srL = toLocalMin(sr.rise, tzOff), ssL = toLocalMin(sr.set, tzOff);
    var grL = msg[Keys.GOLDEN_RISE_MIN], gsL = msg[Keys.GOLDEN_SET_MIN];
    if (grL >= 0 && grL <= srL) grL = srL + 1;
    if (gsL >= 0 && gsL >= ssL) gsL = ssL - 1;
    msg[Keys.GOLDEN_RISE_MIN] = grL;
    msg[Keys.GOLDEN_SET_MIN]  = gsL;
  }

  /* Heutiges Datum merken: naechste Anfrage desselben Tages wird uebersprungen */
  try { localStorage.setItem('solar-sent-date', String(todayInt())); } catch(e) {}
  /* Position fuer die stuendlichen Wetterabrufe merken (ohne neues GPS) */
  try { localStorage.setItem('solar-last-loc', JSON.stringify({ lat: lat, lon: lon })); } catch(e) {}

  console.log('[solar] calcAndSend lat=' + lat.toFixed(3) + ' lon=' + lon.toFixed(3));
  sendMsg(msg, 'astro', function() { fetchWeather(lat, lon, forceWx); });
}

// ─────────────────────────────────────────────
//  Wetter: Open-Meteo (kein API-Schluessel noetig)
//  48 Stundenwerte ab lokaler Mitternacht, je ein Byte:
//    Bit 0-3 Bedeckung in Zehnteln, Bit 4-6 Niederschlagsart
//  dazu Wind (km/h) je Stunde und Hoch/Tief fuer heute und morgen.
// ─────────────────────────────────────────────
var WX_URL = 'https://api.open-meteo.com/v1/forecast';
var WX_HOURS = 48;
var WX_TEMP_OFFSET = 100;                 // muss zu WX_TEMP_OFFSET in solararc.c passen
var WX_MIN_FETCH_MS = 20 * 60 * 1000;     // Drossel fuer Weckrufe der Uhr
var WX_P_NONE = 0, WX_P_RAIN = 1, WX_P_SNOW = 2, WX_P_THUNDER = 3, WX_P_FOG = 4;

function wxMode(s) {
  var m = parseInt(s.WX_MODE, 10);
  return isNaN(m) ? 0 : m;
}

function addWxSettings(msg, s) {
  msg[Keys.WX_MODE] = wxMode(s);
  msg[Keys.WX_SHOW_TEMP] = s.WX_SHOW_TEMP ? 1 : 0;
}

// WMO-Wettercode -> Niederschlagsart
function wxPrecip(code) {
  if (code >= 95) return WX_P_THUNDER;
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return WX_P_SNOW;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return WX_P_RAIN;
  if (code === 45 || code === 48) return WX_P_FOG;
  return WX_P_NONE;
}

function wxPack(cloudPct, precip) {
  var c = Math.round((cloudPct || 0) / 10);
  if (c < 0) c = 0;
  if (c > 10) c = 10;
  return (precip << 4) | c;
}

function wxTempByte(t) {
  if (t === null || t === undefined || isNaN(t)) return 0;
  var v = Math.round(t) + WX_TEMP_OFFSET;
  return v < 1 ? 1 : (v > 255 ? 255 : v);
}

function sendWeather(wx) {
  var msg = {};
  msg[Keys.WX_START] = wx.start;
  msg[Keys.WX_HOURS] = wx.hours;
  msg[Keys.WX_WIND]  = wx.wind;
  msg[Keys.WX_TEMPS] = wx.temps;
  try { localStorage.setItem('solar-wx-time', String(Date.now())); } catch(e) {}
  sendMsg(msg, 'weather');
}

// Testdaten fuer den Emulator: clay-settings.WX_DEMO = 1..5 setzen
// (steht bewusst nicht in der Konfigseite). Index 12 = laufende Stunde.
function wxDemo(n) {
  var nowS = Math.floor(Date.now() / 1000);
  var wx = { start: nowS - (nowS % 3600) - 12 * 3600, hours: [], wind: [],
             temps: [wxTempByte(17), wxTempByte(8), wxTempByte(14), wxTempByte(6)] };
  for (var i = 0; i < WX_HOURS; i++) {
    var k = i - 12, cc = 10, p = WX_P_NONE, w = 8;
    if (n === 1) {            // heiter, nachmittags Schauer
      cc = 30;
      if (k >= 1 && k <= 2) cc = 60;
      if (k >= 3 && k <= 5) { cc = 100; p = WX_P_RAIN; w = 25; }
      if (k >= 6 && k <= 7) cc = 80;
    } else if (n === 2) {     // bedeckt, Schnee
      cc = 100; w = 30;
      if (k >= 1 && k <= 4) p = WX_P_SNOW;
      wx.temps = [wxTempByte(-2), wxTempByte(-7), wxTempByte(-1), wxTempByte(-9)];
    } else if (n === 3) {     // Gewitter am Nachmittag
      cc = (k % 2 === 0) ? 90 : 50; w = 40;
      if (k >= 2 && k <= 3) p = WX_P_THUNDER;
      if (k === 4) p = WX_P_RAIN;
      wx.temps = [wxTempByte(31), wxTempByte(19), wxTempByte(27), wxTempByte(18)];
    } else if (n === 4) {     // Nebel, Flaute, wechselnd bewoelkt
      cc = (k % 3 === 0) ? 70 : 20; w = 1;
      if (k <= 0) { p = WX_P_FOG; cc = 100; }
    } else if (n === 5) {     // Stundenstreifen: duenn, Regen, bewoelkt
      cc = 0; w = 18;
      if (k === -1) cc = 40;
      if (k === 0) { cc = 100; p = WX_P_RAIN; }
      if (k === 1) cc = 70;
    }
    wx.hours.push(wxPack(cc, p));
    wx.wind.push(w);
  }
  return wx;
}

function fetchWeather(lat, lon, force) {
  var s = getStoredSettings();
  if (wxMode(s) === 0) return;
  var last = 0;
  try { last = parseInt(localStorage.getItem('solar-wx-time') || '0', 10); } catch(e) {}
  if (!force && Date.now() - last < WX_MIN_FETCH_MS) {
    console.log('[solar] weather: noch frisch, kein Abruf');
    return;
  }
  var demo = parseInt(s.WX_DEMO, 10);
  if (demo > 0) { sendWeather(wxDemo(demo)); return; }

  var url = WX_URL + '?latitude=' + lat.toFixed(3) + '&longitude=' + lon.toFixed(3) +
    '&hourly=cloud_cover,weather_code,wind_speed_10m' +
    '&daily=temperature_2m_max,temperature_2m_min' +
    '&timezone=auto&forecast_days=2&timeformat=unixtime' +
    (s.WX_FAHRENHEIT ? '&temperature_unit=fahrenheit' : '');
  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    if (xhr.status !== 200) { console.log('[solar] weather HTTP ' + xhr.status); return; }
    try {
      var d = JSON.parse(xhr.responseText);
      var h = d.hourly, dl = d.daily;
      var wx = { start: h.time[0], hours: [], wind: [], temps: [] };
      for (var i = 0; i < WX_HOURS; i++) {
        var ok = i < h.time.length;
        wx.hours.push(ok ? wxPack(h.cloud_cover[i], wxPrecip(h.weather_code[i])) : 0);
        var w = ok ? Math.round(h.wind_speed_10m[i] || 0) : 0;
        wx.wind.push(w > 255 ? 255 : w);
      }
      for (var k = 0; k < 2; k++) {
        wx.temps.push(wxTempByte(dl.temperature_2m_max[k]));
        wx.temps.push(wxTempByte(dl.temperature_2m_min[k]));
      }
      console.log('[solar] weather ok, start=' + wx.start + ' hours=' + JSON.stringify(wx.hours));
      sendWeather(wx);
    } catch (e) {
      console.log('[solar] weather parse: ' + e);
    }
  };
  xhr.onerror = function() { console.log('[solar] weather: Netzwerkfehler'); };
  xhr.open('GET', url);
  xhr.send();
}

// Wetter fuer die zuletzt bekannte Position; ohne Position voller Durchlauf
function fetchWeatherCached() {
  var loc = null;
  try { loc = JSON.parse(localStorage.getItem('solar-last-loc') || 'null'); } catch(e) {}
  if (loc && typeof loc.lat === 'number') fetchWeather(loc.lat, loc.lon, false);
  else getLocationAndSend(false);
}

// ─────────────────────────────────────────────
//  Standort holen und Astronomie senden
// ─────────────────────────────────────────────
function getLocationAndSend(forceWx) {
  var s = getStoredSettings();
  var lat = parseFloat(s.FIXED_LAT), lon = parseFloat(s.FIXED_LON);
  var valid = !isNaN(lat) && !isNaN(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;

  if (s.USE_FIXED_LOC) {
    if (!valid) { lat = FALLBACK_LAT; lon = FALLBACK_LON; }
    calcAndSend(lat, lon, forceWx);
  } else {
    navigator.geolocation.getCurrentPosition(
      function(pos) { calcAndSend(pos.coords.latitude, pos.coords.longitude, forceWx); },
      function(err) {
        console.log('[solar] GPS err ' + err.code + ', Fallback Elmshorn');
        calcAndSend(FALLBACK_LAT, FALLBACK_LON, forceWx);
      },
      { enableHighAccuracy: false, maximumAge: 3600000, timeout: 10000 });
  }
}

// ─────────────────────────────────────────────
//  PebbleKit JS Events
// ─────────────────────────────────────────────
Pebble.addEventListener('ready', function() {
  console.log('[solar] ready');
  /* Beim ersten Start immer vollstaendig senden */
  getLocationAndSend(true);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response || e.response === 'CANCELLED') return;
  /* Settings senden, Standortdaten ERST NACH Abschluss dieser Sendung
   * anstossen (AppMessage kann nur eine Nachricht gleichzeitig transportieren -
   * zwei Sends direkt hintereinander ohne auf ACK/NACK zu warten lassen den
   * zweiten Send fehlschlagen).
   * Standort danach IMMER neu holen/senden, auch wenn heute schon einmal
   * gesendet wurde: Config-Schliessen ist ein seltenes, bewusstes
   * Nutzer-Ereignis (kein Grund, hier am Tages-Cache zu sparen), und nur so
   * wird auch ein Wechsel zwischen fester Position und Automatik (GPS)
   * zuverlaessig uebernommen. Die Tages-Drossel gilt weiterhin fuer die
   * stillen REQUEST_UPDATE-Weckrufe von der Uhr weiter unten. */
  sendSettings(function() {
    console.log('[solar] webviewclosed: Standortdaten werden aktualisiert');
    getLocationAndSend(true);
  });
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload[Keys.REQUEST_WEATHER]) {
    console.log('[solar] REQUEST_WEATHER');
    fetchWeatherCached();
    return;
  }
  if (!e.payload[Keys.REQUEST_UPDATE]) return;

  /* Uhr sendet REQUEST_DATE (yyyymmdd) mit. Wenn das Telefon heute bereits
   * gesendet hat, werden nur die Settings aktualisiert – kein GPS, kein
   * Mondaufgang-Scan, keine NOAA-Berechnung. */
  var watchDate = e.payload[Keys.REQUEST_DATE] || 0;
  var sent = 0;
  try { sent = parseInt(localStorage.getItem('solar-sent-date') || '0', 10); } catch(ex) {}

  if (sent === watchDate && watchDate !== 0) {
    console.log('[solar] REQUEST_UPDATE: heute (' + watchDate + ') bereits gesendet, nur Settings');
    sendSettings(fetchWeatherCached);
  } else {
    console.log('[solar] REQUEST_UPDATE: neuer Tag (' + watchDate + ' vs ' + sent + '), hole Location');
    getLocationAndSend(false);
  }
});