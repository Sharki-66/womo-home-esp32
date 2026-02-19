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
"<!doctype html>\n"
"<html lang=\"de\">\n"
"<head>\n"
"  <meta charset=\"utf-8\" />\n"
"  <meta name=\"viewport\" content=\"width=device-width,initial-scale=1\" />\n"
"  <title>K\u00fcnstlicher Horizont</title>\n"
"  <style>\n"
"    :root{--muted:#9aa6ad;color-scheme:dark;}\n"
"    html,body{height:100%;margin:0;font-family:Inter,system-ui,Segoe UI,Roboto,\"Helvetica Neue\",Arial;background:linear-gradient(#0d1113,#0b0f11);color:#e6eef2;}\n"
"    .wrap{padding:24px;display:flex;justify-content:center;}\n"
"    .card{width:min(640px,96vw);background:linear-gradient(180deg, rgba(255,255,255,0.02), rgba(255,255,255,0.01));border-radius:12px;padding:18px;border:1px solid rgba(255,255,255,0.03);}\n"
"    h1{margin:4px 0 6px;font-size:20px;text-align:center;}\n"
"    .gauge{width:100%;max-width:560px;aspect-ratio:1/1;margin:6px auto;}\n"
"    .info-row{display:flex;gap:6px;justify-content:center;flex-wrap:wrap;margin-top:10px;}\n"
"    .info{background: rgba(255,255,255,0.02);padding:10px 12px;border-radius:10px;min-width:86px;text-align:center;}\n"
"    .info .val{font-weight:700;font-size:18px;color:#fff;}\n"
"    .cal-row{margin-top:12px;background:rgba(255,255,255,0.02);padding:10px 12px;border-radius:10px;}\n"
"    .cal-label{font-size:12px;color:var(--muted);margin-bottom:4px;}\n"
"    .cal-values{display:flex;gap:8px;font-weight:700;font-size:18px;align-items:center;}\n"
"    .cal-0{color:#ff4d4f;}\n"
"    .cal-1{color:#ffb347;}\n"
"    .cal-2{color:#ffe156;}\n"
"    .cal-3{color:#00c853;}\n"
"    .status{margin-top:10px;text-align:center;font-size:14px;color:#e6eef2;min-height:20px;}\n"
"    @media (max-width:420px){h1{font-size:18px}.info .val{font-size:16px}}\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <div class=\"wrap\">\n"
"    <div class=\"card\" role=\"region\" aria-label=\"K\u00fcnstlicher Horizont\">\n"
"      <h1>K\u00fcnstlicher Horizont</h1>\n"
"      <div class=\"gauge\">\n"
"        <svg id=\"svg\" viewBox=\"0 0 400 400\" width=\"100%\" height=\"100%\" xmlns=\"http://www.w3.org/2000/svg\" role=\"img\" aria-label=\"Horizont mit Heading-Laufband\">\n"
"          <defs>\n"
"            <clipPath id=\"circleClip\"><circle cx=\"200\" cy=\"200\" r=\"185\" /></clipPath>\n"
"            <linearGradient id=\"skyGrad\" x1=\"0\" x2=\"0\" y1=\"0\" y2=\"1\">\n"
"              <stop offset=\"0\" stop-color=\"#9fe0ff\"/><stop offset=\"1\" stop-color=\"#7fd1ff\"/>\n"
"            </linearGradient>\n"
"          </defs>\n"
"          <circle cx=\"200\" cy=\"200\" r=\"196\" fill=\"#1a1f22\" opacity=\"0.95\"/>\n"
"          <circle cx=\"200\" cy=\"200\" r=\"188\" fill=\"#0f1416\"/>\n"
"          <circle cx=\"200\" cy=\"200\" r=\"186\" fill=\"none\" stroke=\"rgba(255,255,255,0.04)\" stroke-width=\"1\"/>\n"
"          <g clip-path=\"url(#circleClip)\">\n"
"            <g id=\"horizon\" transform=\"rotate(0 200 200) translate(0,0)\">\n"
"              <rect x=\"-800\" y=\"-800\" width=\"2400\" height=\"1600\" fill=\"url(#skyGrad)\"/>\n"
"              <rect x=\"-800\" y=\"200\" width=\"2400\" height=\"1200\" fill=\"#be7326\"/>\n"
"            </g>\n"
"            <g id=\"heading-clip-group\">\n"
"              <g id=\"heading-band\" transform=\"translate(0,52)\"></g>\n"
"            </g>\n"
"            <g id=\"vscale\" transform=\"translate(200,200)\" fill=\"#fff\" font-family=\"Inter,Arial,Helvetica,sans-serif\"></g>\n"
"            <g id=\"roll-scale-left\"></g>\n"
"            <circle cx=\"200\" cy=\"200\" r=\"185\" fill=\"none\" stroke=\"rgba(0,0,0,0.45)\" stroke-width=\"12\"/>\n"
"            <circle cx=\"200\" cy=\"200\" r=\"186\" fill=\"none\" stroke=\"rgba(255,255,255,0.02)\" stroke-width=\"1\"/>\n"
"          </g>\n"
"          <g id=\"axis-rot\" transform=\"rotate(0 200 200)\">\n"
"            <g transform=\"translate(200,200)\">\n"
"              <line x1=\"-178\" x2=\"178\" y1=\"0\" y2=\"0\" stroke=\"#ffffff\" stroke-width=\"5\" stroke-linecap=\"round\" opacity=\"0.98\"/>\n"
"              <rect x=\"-2\" y=\"-8\" width=\"4\" height=\"16\" rx=\"2\" fill=\"#ffffff\"></rect>\n"
"            </g>\n"
"          </g>\n"
"          <circle cx=\"200\" cy=\"200\" r=\"2.6\" fill=\"#dcdcdc\" stroke=\"rgba(0,0,0,0.18)\" stroke-width=\"0.9\"></circle>\n"
"        </svg>\n"
"      </div>\n"
"      <div class=\"info-row\">\n"
"        <div class=\"info\"><div style=\"font-size:12px;color:var(--muted)\">HEADING</div><div class=\"val\" id=\"headingVal\">--.-\u00b0</div></div>\n"
"        <div class=\"info\"><div style=\"font-size:12px;color:var(--muted)\">ROLL</div><div class=\"val\" id=\"rollVal\">--.-\u00b0</div></div>\n"
"        <div class=\"info\"><div style=\"font-size:12px;color:var(--muted)\">PITCH</div><div class=\"val\" id=\"pitchVal\">--.-\u00b0</div></div>\n"
"      </div>\n"
"      <div class=\"cal-row\">\n"
"        <div class=\"cal-label\">Kalibrierung (SYS / G / A / M)</div>\n"
"        <div class=\"cal-values\">\n"
"          <span class=\"cal-0\" id=\"calSys\">-</span> /\n"
"          <span class=\"cal-0\" id=\"calGyro\">-</span> /\n"
"          <span class=\"cal-0\" id=\"calAccel\">-</span> /\n"
"          <span class=\"cal-0\" id=\"calMag\">-</span>\n"
"        </div>\n"
"      </div>\n"
"      <div class=\"status\" id=\"status\">Verbinde...</div>\n"
"    </div>\n"
"  </div>\n"
"  <script>\n"
"    const SVG_NS='http://www.w3.org/2000/svg';\n"
"    const cx=200,cy=200;\n"
"    const headingValBox=document.getElementById('headingVal');\n"
"    const rollValBox=document.getElementById('rollVal');\n"
"    const pitchValBox=document.getElementById('pitchVal');\n"
"    const calSys=document.getElementById('calSys');\n"
"    const calGyro=document.getElementById('calGyro');\n"
"    const calAccel=document.getElementById('calAccel');\n"
"    const calMag=document.getElementById('calMag');\n"
"    const statusEl=document.getElementById('status');\n"
"    const headingBand=document.getElementById('heading-band');\n"
"    const horizon=document.getElementById('horizon');\n"
"    const axisRot=document.getElementById('axis-rot');\n"
"    const vscale=document.getElementById('vscale');\n"
"    const rollScaleLeft=document.getElementById('roll-scale-left');\n"
"    const pitchFactor=6;\n"
"    const maxScaleValue=15;\n"
"    const halfArc=45;\n"
"    const anglePerUnit=halfArc/maxScaleValue;\n"
"    const scaleR=170;\n"
"    const innerR=154;\n"
"    const labelInnerR=innerR-12;\n"
"    const compass=['N','NNO','NO','ONO','O','OSO','SO','SSO','S','SSW','SW','WSW','W','WNW','NW','NNW'];\n"
"    const pxPerDeg=5.0;\n"
"    const bandY=52;\n"
"    const bandHeight=32;\n"
"    const cycles=6;\n"
"    const pitchLimit=30;\n"
"    const clamp=(v,min,max)=>Math.min(max,Math.max(min,v));\n"
"    function degToRad(d){return d*Math.PI/180;}\n"
"    function buildVScale(){\n"
"      while(vscale.firstChild) vscale.removeChild(vscale.firstChild);\n"
"      for(let deg=-15;deg<=15;deg++){\n"
"        const y=-deg*pitchFactor;\n"
"        const isMajor=(deg%5===0);\n"
"        const x1=-(isMajor?10:4);\n"
"        const x2=(isMajor?10:4);\n"
"        const line=document.createElementNS(SVG_NS,'line');\n"
"        line.setAttribute('x1',x1);line.setAttribute('y1',y);\n"
"        line.setAttribute('x2',x2);line.setAttribute('y2',y);\n"
"        line.setAttribute('stroke','#ffffff');\n"
"        line.setAttribute('stroke-width',isMajor?'1.2':'1.0');\n"
"        line.setAttribute('stroke-linecap','round');\n"
"        vscale.appendChild(line);\n"
"        if(isMajor){\n"
"          const text=document.createElementNS(SVG_NS,'text');\n"
"          text.setAttribute('x',22);text.setAttribute('y',y+4);\n"
"          text.setAttribute('fill','#ffffff');\n"
"          text.setAttribute('font-size','14');\n"
"          text.setAttribute('text-anchor','start');\n"
"          text.textContent=`${deg}\u00b0`;\n"
"          vscale.appendChild(text);\n"
"        }\n"
"      }\n"
"      const dot=document.createElementNS(SVG_NS,'circle');\n"
"      dot.setAttribute('cx',0);dot.setAttribute('cy',0);dot.setAttribute('r',1.8);\n"
"      dot.setAttribute('fill','#dcdcdc');\n"
"      dot.setAttribute('stroke','#000000');\n"
"      dot.setAttribute('stroke-opacity',0.08);\n"
"      dot.setAttribute('stroke-width',0.6);\n"
"      vscale.appendChild(dot);\n"
"    }\n"
"    function buildLeftRollScale(){\n"
"      while(rollScaleLeft.firstChild) rollScaleLeft.removeChild(rollScaleLeft.firstChild);\n"
"      const theta0=degToRad(180);\n"
"      const xOuter0=cx+scaleR*Math.cos(theta0),yOuter0=cy-scaleR*Math.sin(theta0);\n"
"      const xInner0=cx+innerR*Math.cos(theta0),yInner0=cy-innerR*Math.sin(theta0);\n"
"      const line0=document.createElementNS(SVG_NS,'line');\n"
"      line0.setAttribute('x1',xInner0);line0.setAttribute('y1',yInner0);\n"
"      line0.setAttribute('x2',xOuter0);line0.setAttribute('y2',yOuter0);\n"
"      line0.setAttribute('stroke','#fff');line0.setAttribute('stroke-width','3');line0.setAttribute('stroke-linecap','round');\n"
"      rollScaleLeft.appendChild(line0);\n"
"      const lx0=cx+labelInnerR*Math.cos(theta0),ly0=cy-labelInnerR*Math.sin(theta0);\n"
"      const t0=document.createElementNS(SVG_NS,'text');\n"
"      t0.setAttribute('x',lx0);t0.setAttribute('y',ly0);t0.setAttribute('fill','#ffffff');t0.setAttribute('font-size','12');t0.setAttribute('text-anchor','middle');t0.setAttribute('dominant-baseline','middle');t0.textContent='0\u00b0';\n"
"      rollScaleLeft.appendChild(t0);\n"
"      for(let v=1;v<=maxScaleValue;v++){\n"
"        const angleDegUp=180-v*anglePerUnit;\n"
"        const thetaUp=degToRad(angleDegUp);\n"
"        const xOuterUp=cx+scaleR*Math.cos(thetaUp);const yOuterUp=cy-scaleR*Math.sin(thetaUp);\n"
"        const xInnerUp=cx+innerR*Math.cos(thetaUp);const yInnerUp=cy-innerR*Math.sin(thetaUp);\n"
"        const lineUp=document.createElementNS(SVG_NS,'line');\n"
"        lineUp.setAttribute('x1',xInnerUp);lineUp.setAttribute('y1',yInnerUp);\n"
"        lineUp.setAttribute('x2',xOuterUp);lineUp.setAttribute('y2',yOuterUp);\n"
"        lineUp.setAttribute('stroke',(v%5===0)?'#fff':'rgba(255,255,255,0.6)');\n"
"        lineUp.setAttribute('stroke-width',(v%5===0)?'3':'1.2');\n"
"        lineUp.setAttribute('stroke-linecap','round');\n"
"        rollScaleLeft.appendChild(lineUp);\n"
"        if(v%5===0){\n"
"          const lx=cx+labelInnerR*Math.cos(thetaUp);\n"
"          const ly=cy-labelInnerR*Math.sin(thetaUp);\n"
"          const text=document.createElementNS(SVG_NS,'text');\n"
"          text.setAttribute('x',lx);text.setAttribute('y',ly);\n"
"          text.setAttribute('fill','#ffffff');text.setAttribute('font-size','14');\n"
"          text.setAttribute('text-anchor','middle');text.setAttribute('dominant-baseline','middle');\n"
"          text.textContent=`+${v}\u00b0`;\n"
"          rollScaleLeft.appendChild(text);\n"
"        }\n"
"      }\n"
"      for(let v=1;v<=maxScaleValue;v++){\n"
"        const angleDegDown=180+v*anglePerUnit;\n"
"        const thetaDown=degToRad(angleDegDown);\n"
"        const xOuterDown=cx+scaleR*Math.cos(thetaDown);const yOuterDown=cy-scaleR*Math.sin(thetaDown);\n"
"        const xInnerDown=cx+innerR*Math.cos(thetaDown);const yInnerDown=cy-innerR*Math.sin(thetaDown);\n"
"        const lineDown=document.createElementNS(SVG_NS,'line');\n"
"        lineDown.setAttribute('x1',xInnerDown);lineDown.setAttribute('y1',yInnerDown);\n"
"        lineDown.setAttribute('x2',xOuterDown);lineDown.setAttribute('y2',yOuterDown);\n"
"        lineDown.setAttribute('stroke',(v%5===0)?'#fff':'rgba(255,255,255,0.6)');\n"
"        lineDown.setAttribute('stroke-width',(v%5===0)?'3':'1.2');\n"
"        lineDown.setAttribute('stroke-linecap','round');\n"
"        rollScaleLeft.appendChild(lineDown);\n"
"        if(v%5===0){\n"
"          const lx=cx+labelInnerR*Math.cos(thetaDown);\n"
"          const ly=cy-labelInnerR*Math.sin(thetaDown);\n"
"          const text=document.createElementNS(SVG_NS,'text');\n"
"          text.setAttribute('x',lx);text.setAttribute('y',ly);\n"
"          text.setAttribute('fill','#ffffff');text.setAttribute('font-size','14');\n"
"          text.setAttribute('text-anchor','middle');text.setAttribute('dominant-baseline','middle');\n"
"          text.textContent=`-${v}\u00b0`;\n"
"          rollScaleLeft.appendChild(text);\n"
"        }\n"
"      }\n"
"      const markerUp=document.createElementNS(SVG_NS,'circle');markerUp.setAttribute('r','6');markerUp.setAttribute('fill','#ffffff');markerUp.setAttribute('id','marker-up');markerUp.setAttribute('opacity','0');rollScaleLeft.appendChild(markerUp);\n"
"      const markerDown=document.createElementNS(SVG_NS,'circle');markerDown.setAttribute('r','6');markerDown.setAttribute('fill','#ffffff');markerDown.setAttribute('id','marker-down');markerDown.setAttribute('opacity','0');rollScaleLeft.appendChild(markerDown);\n"
"      return {markerUp,markerDown};\n"
"    }\n"
"    function buildHeadingBand(){\n"
"      while(headingBand.firstChild) headingBand.removeChild(headingBand.firstChild);\n"
"      const oneCycleWidth=360*pxPerDeg;\n"
"      const totalWidth=oneCycleWidth*cycles;\n"
"      const startX=cx-totalWidth/2;\n"
"      const bg=document.createElementNS(SVG_NS,'rect');\n"
"      bg.setAttribute('x',startX);bg.setAttribute('y',-bandHeight/2);\n"
"      bg.setAttribute('width',totalWidth);bg.setAttribute('height',bandHeight);\n"
"      bg.setAttribute('rx',8);\n"
"      bg.style.fill='rgba(90,180,230,0.9)';\n"
"      bg.style.stroke='rgba(6,40,60,0.14)';\n"
"      bg.style.strokeWidth='1.2';\n"
"      headingBand.appendChild(bg);\n"
"      for(let cycle=0;cycle<cycles;cycle++){\n"
"        for(let deg=0;deg<360;deg+=22.5){\n"
"          const idx=Math.round(deg/22.5)%16;\n"
"          const x=startX+(cycle*360+deg)*pxPerDeg;\n"
"          const text=document.createElementNS(SVG_NS,'text');\n"
"          text.setAttribute('x',x);\n"
"          text.setAttribute('y',0);\n"
"          text.setAttribute('fill','#ffffff');\n"
"          text.setAttribute('font-size','16');\n"
"          text.setAttribute('font-weight','700');\n"
"          text.setAttribute('text-anchor','middle');\n"
"          text.setAttribute('dominant-baseline','middle');\n"
"          text.textContent=compass[idx];\n"
"          headingBand.appendChild(text);\n"
"        }\n"
"      }\n"
"      return {oneCycleWidth,totalWidth,startX};\n"
"    }\n"
"    function updateHeadingBandTransform(headingDeg,bandInfo){\n"
"      const oneCycleWidth=bandInfo.oneCycleWidth;\n"
"      let rawOffset=(headingDeg*pxPerDeg)%oneCycleWidth;\n"
"      if(rawOffset<0) rawOffset+=oneCycleWidth;\n"
"      const translateX=(oneCycleWidth/2)-rawOffset;\n"
"      headingBand.setAttribute('transform',`translate(${translateX},${bandY})`);\n"
"    }\n"
"    function setLeftMarker(marker,mag,up){\n"
"      const clamped=Math.min(Math.abs(mag),maxScaleValue);\n"
"      const angleDeg=up?(180-clamped*anglePerUnit):(180+clamped*anglePerUnit);\n"
"      const theta=degToRad(angleDeg);\n"
"      const mx=cx+scaleR*Math.cos(theta);\n"
"      const my=cy-scaleR*Math.sin(theta);\n"
"      marker.setAttribute('cx',mx);\n"
"      marker.setAttribute('cy',my);\n"
"      marker.setAttribute('opacity',(Math.abs(mag)>maxScaleValue)?'0.6':'0.98');\n"
"    }\n"
"    buildVScale();\n"
"    const {markerUp,markerDown}=buildLeftRollScale();\n"
"    const bandInfo=buildHeadingBand();\n"
"    function updateAll(heading,roll,pitch){\n"
"      const rollClamped=clamp(roll,-maxScaleValue,maxScaleValue);\n"
"      const pitchClamped=clamp(pitch,-pitchLimit,pitchLimit);\n"
"      updateHeadingBandTransform(heading,bandInfo);\n"
"      headingValBox.textContent=heading.toFixed(1)+'\u00b0';\n"
"      horizon.setAttribute('transform',`rotate(${rollClamped} 200 200) translate(0, ${Math.round(pitchClamped*pitchFactor)})`);\n"
"      axisRot.setAttribute('transform',`rotate(${rollClamped} 200 200)`);\n"
"      if(roll>0){\n"
"        setLeftMarker(markerUp,roll,true);\n"
"        markerUp.setAttribute('opacity',(Math.abs(roll)>maxScaleValue)?'0.6':'0.98');\n"
"        markerDown.setAttribute('opacity','0');\n"
"      }else if(roll<0){\n"
"        setLeftMarker(markerDown,roll,false);\n"
"        markerDown.setAttribute('opacity',(Math.abs(roll)>maxScaleValue)?'0.6':'0.98');\n"
"        markerUp.setAttribute('opacity','0');\n"
"      }else{\n"
"        markerUp.setAttribute('opacity','0');\n"
"        markerDown.setAttribute('opacity','0');\n"
"      }\n"
"      rollValBox.textContent=roll.toFixed(1)+'\u00b0';\n"
"      pitchValBox.textContent=pitch.toFixed(1)+'\u00b0';\n"
"    }\n"
"    async function refresh(){\n"
"      try{\n"
"        const rsp=await fetch('/api/imu',{cache:'no-store'});\n"
"        if(!rsp.ok) throw new Error('HTTP '+rsp.status);\n"
"        const data=await rsp.json();\n"
"        if(!data.ok){statusEl.textContent='Keine IMU-Daten verf\\u00fcgbar';return;}\n"
"        updateAll(data.yaw_deg, data.roll_deg, data.pitch_deg);\n"
"        if(data.cal && data.cal.length===4){\n"
"          const vals=[Number(data.cal[0]),Number(data.cal[1]),Number(data.cal[2]),Number(data.cal[3])];\n"
"          const clampVal=v=>Math.max(0,Math.min(3,isFinite(v)?v:0));\n"
"          const colors=['cal-0','cal-1','cal-2','cal-3'];\n"
"          const setCal=(el,val)=>{const v=clampVal(val);colors.forEach(c=>el.classList.remove(c));el.classList.add(colors[v]);el.textContent=v;};\n"
"          setCal(calSys,vals[0]);\n"
"          setCal(calGyro,vals[1]);\n"
"          setCal(calAccel,vals[2]);\n"
"          setCal(calMag,vals[3]);\n"
"        }\n"
"        statusEl.textContent=data.calibrated?'Kalibriert':'Nicht kalibriert';\n"
"      }catch(err){\n"
"        statusEl.textContent='Verbindung fehlgeschlagen';\n"
"        console.warn('Verbindung fehlgeschlagen',err);\n"
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
