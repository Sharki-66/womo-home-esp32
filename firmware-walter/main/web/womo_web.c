#include "web/womo_web.h"

#include <inttypes.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#if WALTER_ENABLE_WEBUI
static const char *TAG = "womo_web";
static httpd_handle_t s_httpd;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static womo_web_imu_sample_t s_latest_sample;

static const char kIndexHtml[] =
"<!DOCTYPE html>\n"
"<html lang=\"de\">\n"
"<head>\n"
"  <meta charset=\"utf-8\">\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
"  <title>Walter IMU Monitor</title>\n"
"  <style>\n"
"    :root{color-scheme:dark;--pitch-span:95%;}\n"
"    body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;margin:0;padding:1.5rem;background:#0d1117;color:#f0f6fc;}\n"
"    h1{font-size:1.4rem;margin:0 0 1rem;}\n"
"    .card{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:1rem;max-width:620px;margin:0 auto;}\n"
"    .status{margin-bottom:1rem;font-size:0.9rem;}\n"
"    .horizon{position:relative;width:100%;aspect-ratio:1/1;margin:0 auto 1.25rem;max-width:360px;}\n"
"    .horizon__circle{position:absolute;inset:0;border-radius:50%;border:2px solid #30363d;background:#0d1117;overflow:hidden;}\n"
"    .horizon__disc{position:absolute;inset:-48%;border-radius:50%;background:linear-gradient(#7fcfff 50%,#c67a2d 50%);box-shadow:0 0 18px rgba(0,0,0,0.45);transition:transform 150ms ease;z-index:1;}\n"
"    .horizon__zero{position:absolute;left:50%;top:50%;width:125%;height:3px;background:#ffffff;transform:translate(-50%,-50%);transform-origin:50% 50%;z-index:3;box-shadow:0 0 6px rgba(0,0,0,0.35);}\n"
"    .horizon__axis{position:absolute;left:50%;top:6%;bottom:6%;width:2px;background:#ffffff;opacity:0.65;z-index:2;}\n"
"    .scale{position:absolute;pointer-events:none;color:#f8fbff;font-size:0.75rem;font-weight:600;z-index:4;}\n"
"    .scale__entry{position:relative;display:flex;align-items:center;gap:4px;}\n"
"    .scale__tick{display:block;height:2px;background:#f8fbff;}\n"
"    .scale__entry--minor .scale__tick{opacity:0.45;}\n"
"    .scale--pitch{position:absolute;left:50%;top:6%;bottom:6%;width:0;}\n"
"    .scale--pitch .scale__entry{position:absolute;top:calc(50% - (var(--deg)/30)*var(--pitch-span));left:50%;transform:translate(-50%,-50%);}\n"
"    .scale--pitch-major .scale__tick--axis{width:36px;height:3px;}\n"
"    .scale--pitch-minor .scale__tick--axis{left:-9px;width:18px;height:1px;opacity:0.4;}\n"
"    .scale__entry-axis{width:0;height:0;}\n"
"    .scale__tick--axis{position:absolute;left:-18px;background:#f8fbff;border-radius:2px;}\n"
"    .scale__label--right{position:absolute;left:18px;}\n"
"    .scale__label--left{position:absolute;right:18px;text-align:right;}\n"
"    .scale--roll-arc{position:absolute;inset:0;--roll-radius:120px;--tick-length:24px;}\n"
"    .scale--roll-arc .scale__entry{position:absolute;left:50%;top:50%;display:flex;align-items:center;gap:6px;transform-origin:0 50%;transform:rotate(calc(var(--angle)*1deg)) translateX(var(--roll-radius));}\n"
"    .scale--roll-leftArc .scale__entry{flex-direction:row-reverse;}\n"
"    .scale--roll-arc .scale__tick{width:var(--tick-length);height:3px;border-radius:2px;background:#f8fbff;box-shadow:0 0 6px rgba(0,0,0,0.3);}\n"
"    .scale--roll-arc .scale__label{display:inline-block;font-size:0.75rem;min-width:26px;transform:rotate(calc(var(--angle)*-1deg));}\n"
"    .scale--roll-leftArc .scale__label{text-align:right;}\n"
"    .scale--roll-rightArc .scale__label{text-align:left;}\n"
"    .scale--roll-leftArc .scale__label{text-align:right;}\n"
"    .scale--roll-rightArc .scale__label{text-align:left;}\n"
"    .scale__tick--major{height:3px;}\n"
"    .scale__tick--minor{height:2px;opacity:0.55;}\n"
"    .info-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:0.6rem;}\n"
"    .info{display:flex;flex-direction:column;background:#0d1117;border:1px solid #30363d;border-radius:10px;padding:0.65rem;}\n"
"    .info__label{font-size:0.78rem;color:#8b949e;margin-bottom:0.2rem;text-transform:uppercase;letter-spacing:0.04em;}\n"
"    .info__value{font-size:1.25rem;font-weight:600;}\n"
"    .info--full{grid-column:1/-1;}\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <div class=\"card\">\n"
"    <h1>Künstlicher Horizont</h1>\n"
"    <div class=\"status\" id=\"status\">Verbinde …</div>\n"
"    <div class=\"horizon\">\n"
"      <div class=\"horizon__circle\" id=\"horizonCircle\">\n"
"        <div class=\"horizon__disc\" id=\"horizonDisc\"></div>\n"
"        <div class=\"horizon__zero\" id=\"zeroLine\"></div>\n"
"        <div class=\"horizon__axis\"></div>\n"
"        <div class=\"scale scale--pitch scale--pitch-minor\" id=\"pitchMinorScale\"></div>\n"
"        <div class=\"scale scale--pitch scale--pitch-major\">\n"
"          <div class=\"scale__entry scale__entry-axis\" style=\"--deg:15\"><span class=\"scale__tick scale__tick--axis\"></span><span class=\"scale__label scale__label--right\">15°</span></div>\n"
"          <div class=\"scale__entry scale__entry-axis\" style=\"--deg:10\"><span class=\"scale__tick scale__tick--axis\"></span><span class=\"scale__label scale__label--right\">10°</span></div>\n"
"          <div class=\"scale__entry scale__entry-axis\" style=\"--deg:5\"><span class=\"scale__tick scale__tick--axis\"></span><span class=\"scale__label scale__label--right\">5°</span></div>\n"
"          <div class=\"scale__entry scale__entry-axis\" style=\"--deg:0\"><span class=\"scale__tick scale__tick--axis\"></span></div>\n"
"          <div class=\"scale__entry scale__entry-axis\" style=\"--deg:-5\"><span class=\"scale__label scale__label--left\">-5°</span><span class=\"scale__tick scale__tick--axis\"></span></div>\n"
"          <div class=\"scale__entry scale__entry-axis\" style=\"--deg:-10\"><span class=\"scale__label scale__label--left\">-10°</span><span class=\"scale__tick scale__tick--axis\"></span></div>\n"
"          <div class=\"scale__entry scale__entry-axis\" style=\"--deg:-15\"><span class=\"scale__label scale__label--left\">-15°</span><span class=\"scale__tick scale__tick--axis\"></span></div>\n"
"        </div>\n"
"        <div class=\"scale scale--roll-arc scale--roll-leftArc\" id=\"rollPosScale\"></div>\n"
"        <div class=\"scale scale--roll-arc scale--roll-rightArc\" id=\"rollNegScale\"></div>\n"
"      </div>\n"
"    </div>\n"
"    <div class=\"info-grid\">\n"
"      <div class=\"info\"><div class=\"info__label\">Heading</div><div class=\"info__value\" id=\"yaw\">–</div></div>\n"
"      <div class=\"info\"><div class=\"info__label\">Roll</div><div class=\"info__value\" id=\"roll\">–</div></div>\n"
"      <div class=\"info\"><div class=\"info__label\">Pitch</div><div class=\"info__value\" id=\"pitch\">–</div></div>\n"
"      <div class=\"info\"><div class=\"info__label\">Temp</div><div class=\"info__value\" id=\"temp\">–</div></div>\n"
"      <div class=\"info info--full\"><div class=\"info__label\">Kalibrierung (SYS / G / A / M)</div><div class=\"info__value\" id=\"cal\">– / – / – / –</div></div>\n"
"    </div>\n"
"  </div>\n"
"  <script>\n"
"    const statusEl=document.getElementById('status');\n"
"    const yawEl=document.getElementById('yaw');\n"
"    const rollEl=document.getElementById('roll');\n"
"    const pitchEl=document.getElementById('pitch');\n"
"    const tempEl=document.getElementById('temp');\n"
"    const calEl=document.getElementById('cal');\n"
"    const guidanceEl=null;\n"
"    const disc=document.getElementById('horizonDisc');\n"
"    const zeroLine=document.getElementById('zeroLine');\n"
"    const pitchMinorScale=document.getElementById('pitchMinorScale');\n"
"    const horizonCircle=document.getElementById('horizonCircle');\n"
"    const rollArcElements=document.querySelectorAll('.scale--roll-arc');\n"
"    const rollPosScale=document.getElementById('rollPosScale');\n"
"    const rollNegScale=document.getElementById('rollNegScale');\n"
"    const clamp=(val,min,max)=>Math.min(max,Math.max(min,val));\n"
"    const TOLERANCE_DEG=1.0;\n"
"    const PITCH_SPAN_PCT=95;\n"
"    const PITCH_SPAN_RATIO=PITCH_SPAN_PCT/100;\n"
"    const PITCH_SCALE_MARGIN_PCT=6;\n"
"    const PITCH_SCALE_HEIGHT_RATIO=(100-2*PITCH_SCALE_MARGIN_PCT)/100;\n"
"    const ROLL_TICKS=[5,10,15];\n"
"    const createRollEntry=(value,isPositive)=>{\n"
"      const entry=document.createElement('div');\n"
"      entry.className='scale__entry';\n"
"      const tick=document.createElement('span');\n"
"      tick.className='scale__tick scale__tick--major';\n"
"      const label=document.createElement('span');\n"
"      label.className='scale__label';\n"
"      label.textContent=(isPositive?value:-value)+'°';\n"
"      if(isPositive){\n"
"        entry.appendChild(label);\n"
"        entry.appendChild(tick);\n"
"      }else{\n"
"        entry.appendChild(tick);\n"
"        entry.appendChild(label);\n"
"      }\n"
"      return entry;\n"
"    };\n"
"    const rollPosEntries=ROLL_TICKS.map(v=>{const el=createRollEntry(v,true);rollPosScale.appendChild(el);return el;});\n"
"    const rollNegEntries=ROLL_TICKS.map(v=>{const el=createRollEntry(v,false);rollNegScale.appendChild(el);return el;});\n"
"    for(let deg=-15;deg<=15;deg++){\n"
"      if(deg%5===0) continue;\n"
"      const entry=document.createElement('div');\n"
"      entry.className='scale__entry scale__entry-axis';\n"
"      entry.style.setProperty('--deg',deg);\n"
"      const tick=document.createElement('span');\n"
"      tick.className='scale__tick scale__tick--axis';\n"
"      entry.appendChild(tick);\n"
"      pitchMinorScale.appendChild(entry);\n"
"    }\n"
"    async function refresh(){\n"
"      try{\n"
"        const rsp=await fetch('/api/imu',{cache:'no-store'});\n"
"        if(!rsp.ok) throw new Error('HTTP '+rsp.status);\n"
"        const data=await rsp.json();\n"
"        if(!data.ok){statusEl.textContent='Keine IMU-Daten verfügbar';return;}\n"
"        statusEl.textContent=data.calibrated?'Kalibriert':'Nicht kalibriert';\n"
"        yawEl.textContent=data.yaw_deg.toFixed(1)+'°';\n"
"        rollEl.textContent=data.roll_deg.toFixed(1)+'°';\n"
"        pitchEl.textContent=data.pitch_deg.toFixed(1)+'°';\n"
"        tempEl.textContent=data.temperature_c.toFixed(1)+'°C';\n"
"        calEl.textContent=data.cal.join(' / ');\n"
"        const pitch=data.pitch_deg;\n"
"        const roll=data.roll_deg;\n"
"        const pitchClamp=clamp(pitch,-15,15);\n"
"        const rollClamp=clamp(roll,-15,15);\n"
"        const circlePx=horizonCircle?Math.min(horizonCircle.clientWidth,horizonCircle.clientHeight):0;\n"
"        const rollRadiusPx=Math.max(0,(circlePx*0.5)-2);\n"
"        rollArcElements.forEach(el=>el.style.setProperty('--roll-radius',rollRadiusPx+'px'));\n"
"        const rollSpanDeg=90;\n"
"        rollPosEntries.forEach((entry,index)=>{\n"
"          const norm=ROLL_TICKS[index]/15;\n"
"          const cssAngle=-(90+norm*rollSpanDeg);\n"
"          entry.style.setProperty('--angle',cssAngle);\n"
"        });\n"
"        rollNegEntries.forEach((entry,index)=>{\n"
"          const norm=ROLL_TICKS[index]/15;\n"
"          const cssAngle=norm*rollSpanDeg;\n"
"          entry.style.setProperty('--angle',cssAngle);\n"
"        });\n"
"        const pitchScalePx=circlePx*PITCH_SCALE_HEIGHT_RATIO;\n"
"        const pitchShiftPx=-(pitchClamp/30)*(pitchScalePx*PITCH_SPAN_RATIO);\n"
"        disc.style.transform='translate3d(0,'+pitchShiftPx+'px,0) rotate('+(-rollClamp)+'deg)';\n"
"        zeroLine.style.transform='translate(-50%,-50%) rotate('+(-rollClamp)+'deg)';\n"
"        if(guidanceEl){\n"
"          const hints=[];\n"
"          if(pitch>TOLERANCE_DEG){hints.push('Vorne absenken');}\n"
"          else if(pitch<-TOLERANCE_DEG){hints.push('Vorne anheben');}\n"
"          if(roll>TOLERANCE_DEG){hints.push('Rechts anheben');}\n"
"          else if(roll<-TOLERANCE_DEG){hints.push('Links anheben');}\n"
"          guidanceEl.textContent=hints.length?hints.join(' · '):'Schon perfekt – Kaffeezeit!';\n"
"        }\n"
"      }catch(err){\n"
"        statusEl.textContent='Verbindung fehlgeschlagen';\n"
"      }\n"
"    }\n"
"    refresh();\n"
"    setInterval(refresh,500);\n"
"  </script>\n"
"</body>\n"
"</html>\n";

static bool snapshot_latest(womo_web_imu_sample_t *out)
{
    bool have = false;
    portENTER_CRITICAL(&s_state_lock);
    if (s_latest_sample.valid) {
        *out = s_latest_sample;
        have = true;
    }
    portEXIT_CRITICAL(&s_state_lock);
    return have;
}

static esp_err_t handle_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_imu(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, max-age=0");

    womo_web_imu_sample_t sample;
    if (!snapshot_latest(&sample)) {
        static const char kNoData[] = "{\"ok\":false}";
        return httpd_resp_send(req, kNoData, HTTPD_RESP_USE_STRLEN);
    }

    char payload[320];
    int len = snprintf(payload,
                       sizeof(payload),
                       "{\"ok\":true,\"yaw_deg\":%.2f,\"pitch_deg\":%.2f,\"roll_deg\":%.2f,"
                       "\"temperature_c\":%.2f,\"calibrated\":%s,\"fallback\":%s,"
                       "\"cal\":[%u,%u,%u,%u],\"ts_us\":%" PRId64 "}",
                       sample.yaw_deg,
                       sample.pitch_deg,
                       sample.roll_deg,
                       sample.temperature_c,
                       sample.calibrated ? "true" : "false",
                       sample.fallback ? "true" : "false",
                       sample.cal_sys,
                       sample.cal_gyro,
                       sample.cal_accel,
                       sample.cal_mag,
                       (int64_t)sample.timestamp_us);

    if (len < 0 || len >= (int)sizeof(payload)) {
        ESP_LOGW(TAG, "IMU JSON truncated");
        len = sizeof(payload) - 1;
        payload[len] = '\0';
    }

    return httpd_resp_send(req, payload, len);
}

esp_err_t womo_web_start(void)
{
    if (s_httpd) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WALTER_WEB_SERVER_PORT;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_index,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &root_uri);

    httpd_uri_t imu_uri = {
        .uri = "/api/imu",
        .method = HTTP_GET,
        .handler = handle_imu,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &imu_uri);

    ESP_LOGI(TAG, "Web UI started on port %d", config.server_port);
    return ESP_OK;
}

void womo_web_publish_imu(const womo_web_imu_sample_t *sample)
{
    if (!sample) {
        return;
    }

    portENTER_CRITICAL(&s_state_lock);
    s_latest_sample = *sample;
    s_latest_sample.valid = true;
    portEXIT_CRITICAL(&s_state_lock);
}

#endif // WALTER_ENABLE_WEBUI
