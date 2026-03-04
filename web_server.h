#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// Зовнішні змінні з chat.ino
extern char WIFI_SSID[33];
extern char WIFI_PASS[65];
extern char AP_SSID[33]; 
extern char AP_PASS[65];
extern bool IS_PRO_MODE;
extern String DEVICE_MAC;
extern const char* PROXY_HOST; // Адреса вашого воркера

WebServer server(80);
extern Preferences preferences;
File uploadFile;

// =================================================================
// =================== HTML СТОРІНКА ===============================
// =================================================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="uk">
<head>
  <meta charset="UTF-8">
  <title>ESP32 Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; transition: background-color 0.3s, color 0.3s; line-height: 1.5; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }
    .light { background-color: #f2f4f8; color: #333; }
    .dark { background-color: #121212; color: #e0e0e0; }
    .container { width: 100%; max-width: 800px; margin: 0 auto; padding: 10px; box-sizing: border-box; }
    
    /* TABS */
    .tab { display: flex; border-radius: 12px; background: rgba(0,0,0,0.05); padding: 4px; margin-bottom: 15px; }
    .dark .tab { background: rgba(255,255,255,0.05); }
    .tab button { flex: 1; background: transparent; border: none; padding: 10px; border-radius: 8px; font-weight: 600; font-size: 14px; color: inherit; cursor: pointer; transition: 0.2s; }
    .tab button.active { background: #fff; box-shadow: 0 2px 4px rgba(0,0,0,0.1); color: #007bff; }
    .dark .tab button.active { background: #2c2c2c; color: #4da3ff; }
    .tabcontent { display: none; animation: fadeIn 0.3s; }
    @keyframes fadeIn { from {opacity: 0; transform: translateY(5px);} to {opacity: 1; transform: translateY(0);} }

    /* CARDS */
    .card { background: #fff; border-radius: 16px; margin-bottom: 15px; box-shadow: 0 2px 8px rgba(0,0,0,0.05); overflow: hidden; }
    .dark .card { background: #1e1e1e; box-shadow: none; border: 1px solid #333; }
    .card-header { padding: 15px; border-bottom: 1px solid #eee; font-weight: 700; font-size: 16px; display: flex; align-items: center; justify-content: space-between; }
    .dark .card-header { border-color: #333; }
    .card-body { padding: 15px; }

    /* PRO STYLES */
    .pro-center { text-align: center; padding: 30px 20px; }
    .pro-icon-lg { font-size: 60px; margin-bottom: 15px; display: block; }
    .pro-badge { background: linear-gradient(135deg, #FFD700, #FFA500); color: #000; padding: 2px 8px; border-radius: 20px; font-size: 10px; font-weight: 800; vertical-align: top; margin-left: 5px; display: none; }
    .pro-active-tab .pro-badge { display: inline-block; }
    .mac-address { font-family: monospace; background: rgba(0,0,0,0.05); padding: 5px 10px; border-radius: 5px; display: inline-block; margin-bottom: 15px; color: #555; }
    .dark .mac-address { background: rgba(255,255,255,0.1); color: #ccc; }
    
    .btn-buy { background: #f55036; width: 100%; color: white; border: none; padding: 14px; border-radius: 10px; font-size: 16px; font-weight: bold; cursor: pointer; margin-bottom: 10px; display: block; text-decoration: none; box-sizing: border-box; }
    .btn-buy:active { transform: scale(0.98); opacity: 0.9; }

    /* FORMS */
    label { display: block; margin-bottom: 6px; font-size: 13px; color: #666; font-weight: 600; }
    .dark label { color: #aaa; }
    input[type='text'], input[type='password'], select { width: 100%; padding: 12px; border: 1px solid #ddd; border-radius: 10px; font-size: 16px; background: #f9f9f9; box-sizing: border-box; color: inherit; outline: none; transition: 0.2s; }
    .dark input, .dark select { background: #2c2c2c; border-color: #444; }
    input:focus { border-color: #007bff; background: #fff; }
    .dark input:focus { background: #333; }
    .btn-save { width: 100%; padding: 14px; border: none; border-radius: 10px; background: #007bff; color: white; font-size: 16px; font-weight: 600; cursor: pointer; margin-top: 10px; }

    /* TABLE */
    table { width: 100%; border-collapse: collapse; table-layout: fixed; }
    td { padding: 12px 5px; border-bottom: 1px solid #f5f5f5; vertical-align: middle; }
    .dark td { border-color: #2a2a2a; }
    td:first-child { width: 60%; word-wrap: break-word; }
    td:last-child { width: 40%; text-align: right; }
    .actions { display: flex; gap: 6px; justify-content: flex-end; }
    .btn-icon { width: 34px; height: 34px; border-radius: 8px; border: none; display: flex; align-items: center; justify-content: center; cursor: pointer; color: #fff; }
    .btn-dl { background: #007bff; } .btn-ren { background: #ffc107; color: #333; } .btn-del { background: #ff4757; }
    .btn-icon svg { width: 18px; height: 18px; fill: currentColor; }

    .theme-switch { position: absolute; top: 15px; right: 15px; font-size: 24px; cursor: pointer; z-index: 10; user-select: none;}
  </style>
</head><body>

<div class="theme-switch" onclick="toggleTheme()" id="themeIcon">🌙</div>

<div class="container">
  <h2 style="margin: 10px 0 20px 5px;">AI Assistant</h2>
  
  <div class="tab">
    <button class="tablinks active" onclick="openTab(event, 'Files')">Файли</button>
    <button class="tablinks" onclick="openTab(event, 'Settings')">Налаштування</button>
    <button class="tablinks" id="tabProBtn" onclick="openTab(event, 'Pro')">Pro <span class="pro-badge">PRO</span></button>
  </div>

  <div id="Files" class="tabcontent" style="display: block;">
    <div class="card">
      <div class="card-body" style="padding: 0;">
        <div style="padding: 15px; border-bottom: 1px solid #eee;">
           <select id="sortOrder" onchange="sortFiles()">
             <option value="default">Сортування: За датою</option>
             <option value="name_asc">Ім'я (A-Z)</option>
             <option value="size_desc">Розмір (Більші)</option>
           </select>
        </div>
        <div style="overflow-x: hidden;"> 
          <table><tbody id="fileList"></tbody></table>
        </div>
      </div>
    </div>
    <div class="card">
      <div class="card-header">Завантаження файлу</div>
      <div class="card-body">
         <form id="uploadForm" style="display: flex; gap: 10px; flex-direction: column;">
            <input type="file" name="uploadFile" id="uploadFile">
            <input type="submit" value="Завантажити" class="btn-save">
         </form>
      </div>
    </div>
  </div>

  <div id="Settings" class="tabcontent">
    <div class="card">
      <div class="card-header">Wi-Fi Клієнт</div>
      <div class="card-body">
        <label>Ім'я мережі (SSID)</label><input type="text" id="ssid">
        <div style="height: 10px;"></div>
        <label>Пароль</label><input type="password" id="pass">
      </div>
    </div>
    <div class="card">
      <div class="card-header">Точка Доступу (Hotspot)</div>
      <div class="card-body">
        <label>Ім'я пристрою</label><input type="text" id="ap_ssid">
        <div style="height: 10px;"></div>
        <label>Пароль (мін 8 симв.)</label><input type="text" id="ap_pass">
        <button class="btn-save" onclick="saveCredentials()">Зберегти та Перезавантажити</button>
      </div>
    </div>
    <div class="card">
      <div class="card-header">Оновлення Системи</div>
      <div class="card-body">
        <div id="updateStatus" style="display: none; padding: 10px; background: #e3f2fd; color: #0d47a1; border-radius: 8px; margin-bottom: 10px; font-size: 14px;"></div>
        <input type="file" id="firmwareFile" accept=".bin">
        <button class="btn-save" style="background: #6c757d;" onclick="updateFirmware()">Прошити (OTA)</button>
      </div>
    </div>
  </div>

  <div id="Pro" class="tabcontent">
    <div class="card">
      <div class="card-body pro-center">
        <div class="pro-icon-lg">👑</div>
        <div id="macDisplay" class="mac-address">Завантаження MAC...</div>
        
        <h2 id="proStatusTitle">Free Версія</h2>
        <p id="proStatusText" style="color: #666; margin-bottom: 25px;">
           Ліміт запису: 5 сек<br>
           Стислі відповіді
        </p>
        
        <div id="proActions">
          <a id="buyLink" href="#" target="_blank" class="btn-buy">
             Оформити підписку (80 ₴)
          </a>
          <p style="font-size: 12px; color: #888; margin-top: 5px;">*Потрібен інтернет на телефоні</p>
          
          <div style="margin-top: 20px; border-top: 1px solid #eee; padding-top: 20px;">
             <p style="font-size: 12px; color: #aaa;">Або введіть ключ вручну:</p>
             <input type="text" id="proKey" placeholder="Ключ активації" style="text-align: center; margin-bottom: 10px;">
             <button class="btn-save" onclick="activatePro()" style="background: #6c757d; font-size: 14px; padding: 10px;">Активувати</button>
          </div>
        </div>
        
        <div id="proSuccessMsg" style="display:none; color: #28a745; font-weight: bold; margin-top: 15px;">
           ✅ Всі функції розблоковано!<br>
           Перезавантажте пристрій.
        </div>
      </div>
    </div>
  </div>
</div>

<script>
  const ICON_DL = `<svg viewBox="0 0 24 24"><path d="M19.35 10.04C18.67 6.59 15.64 4 12 4 9.11 4 6.6 5.64 5.35 8.04 2.34 8.36 0 10.91 0 14c0 3.31 2.69 6 6 6h13c2.76 0 5-2.24 5-5 0-2.64-2.05-4.78-4.65-4.96zM17 13l-5 5-5-5h3V9h4v4h3z"/></svg>`;
  const ICON_REN = `<svg viewBox="0 0 24 24"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>`;
  const ICON_DEL = `<svg viewBox="0 0 24 24"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>`;
  const ICON_FILE = `📄`;
  const ICON_FOLDER = `📁`;

  function openTab(evt, tabName) {
    var i, x, tablinks;
    x = document.getElementsByClassName("tabcontent");
    for (i = 0; i < x.length; i++) { x[i].style.display = "none"; }
    tablinks = document.getElementsByClassName("tablinks");
    for (i = 0; i < tablinks.length; i++) { tablinks[i].className = tablinks[i].className.replace(" active", ""); }
    document.getElementById(tabName).style.display = "block";
    evt.currentTarget.className += " active";
  }

  function toggleTheme() {
    const isDark = document.body.classList.toggle('dark');
    document.body.classList.toggle('light', !isDark);
    document.getElementById('themeIcon').innerText = isDark ? '☀️' : '🌙';
    localStorage.setItem('theme', isDark ? 'dark' : 'light');
    fetch(`/save-theme?theme=${isDark ? 'dark' : 'light'}`);
  }

  function applyTheme() {
    const theme = localStorage.getItem('theme');
    const isDark = theme === 'dark';
    if (isDark) { document.body.classList.add('dark'); document.body.classList.remove('light'); }
    else { document.body.classList.add('light'); document.body.classList.remove('dark'); }
    document.getElementById('themeIcon').innerText = isDark ? '☀️' : '🌙';
  }

  function loadFiles() { fetch('/list-files').then(r => r.json()).then(data => { window.fileData = data; sortFiles(); }); }
  
  function sortFiles() {
    const sortValue = document.getElementById('sortOrder').value;
    let sortedData = [...window.fileData];
    if(sortValue === 'name_asc') sortedData.sort((a,b)=>a.name.localeCompare(b.name));
    else if(sortValue === 'size_desc') sortedData.sort((a,b)=>b.size - a.size);
    else sortedData.sort((a, b) => { if (a.isDir && !b.isDir) return -1; if (!a.isDir && b.isDir) return 1; return b.date - a.date; });
    renderFiles(sortedData);
  }

  function renderFiles(data) {
    const tbody = document.getElementById('fileList'); tbody.innerHTML = '';
    data.forEach(f => {
      const icon = f.isDir ? ICON_FOLDER : ICON_FILE;
      const sizeStr = f.isDir ? '' : (f.size > 1024 ? (f.size/1024).toFixed(1)+' KB' : f.size+' B');
      const row = `<tr><td><div style="display:flex;flex-direction:column;"><a href="/download?file=${encodeURIComponent(f.name)}" style="text-decoration:none;color:inherit;"><span style="font-size:15px;">${icon} ${f.name}</span><span style="font-size:11px;color:#888;">${sizeStr}</span></a></div></td><td><div class="actions"><button class="btn-icon btn-dl" onclick="window.location.href='/download?file=${encodeURIComponent(f.name)}'">${ICON_DL}</button><button class="btn-icon btn-ren" onclick="renameFile('${f.name}')">${ICON_REN}</button><button class="btn-icon btn-del" onclick="deleteFile('${f.name}')">${ICON_DEL}</button></div></td></tr>`;
      tbody.innerHTML += row;
    });
  }

  function deleteFile(file) { if (confirm('Видалити '+file+'?')) fetch(`/delete?file=${encodeURIComponent(file)}`).then(loadFiles); }
  function renameFile(oldName) { const newName = prompt('Нове ім\'я:', oldName); if (newName && newName !== oldName) fetch(`/rename?old=${encodeURIComponent(oldName)}&new=${encodeURIComponent(newName)}`).then(loadFiles); }
  
  function saveCredentials() { 
    const ssid = document.getElementById('ssid').value;
    const pass = document.getElementById('pass').value;
    const ap_ssid = document.getElementById('ap_ssid').value;
    const ap_pass = document.getElementById('ap_pass').value;
    if (ap_pass.length > 0 && ap_pass.length < 8) { alert("Пароль AP має бути мін. 8 символів!"); return; }
    fetch(`/save-credentials?ssid=${encodeURIComponent(ssid)}&pass=${encodeURIComponent(pass)}&ap_ssid=${encodeURIComponent(ap_ssid)}&ap_pass=${encodeURIComponent(ap_pass)}`).then(r => alert('Збережено! Перезавантаження...')); 
  }

  function activatePro() {
    fetch(`/activate-pro?key=${encodeURIComponent(document.getElementById('proKey').value)}`)
      .then(r => r.json()).then(data => {
        if(data.success) { alert("Успіх!"); location.reload(); } else { alert("Невірний ключ"); }
      });
  }

  function updateFirmware() { 
    const file = document.getElementById('firmwareFile').files[0];
    if (!file) return alert("Оберіть файл!");
    const formData = new FormData(); formData.append('firmware', file);
    const st = document.getElementById('updateStatus');
    st.style.display = 'block'; st.innerText = 'Завантаження... Не вимикайте!';
    fetch('/update', { method: 'POST', body: formData }).then(r=>r.json()).then(d=>{
        if(d.status==='ok') { st.innerText='Готово! Перезавантаження...'; } else { st.innerText='Помилка!'; }
    }).catch(e=>{ st.innerText='Готово! Перезавантаження...'; });
  }

  document.getElementById('uploadForm').addEventListener('submit', e => { e.preventDefault(); const fd = new FormData(); fd.append('uploadFile', document.getElementById('uploadFile').files[0]); fetch('/upload', { method: 'POST', body: fd }).then(loadFiles); });
  
  window.onload = () => { 
    applyTheme(); loadFiles(); 
    fetch('/get-credentials').then(r => r.json()).then(d => { 
        document.getElementById('ssid').value = d.ssid; 
        document.getElementById('pass').value = d.pass; 
        document.getElementById('ap_ssid').value = d.ap_ssid;
        document.getElementById('ap_pass').value = d.ap_pass; 
        if(d.mac) {
            document.getElementById('macDisplay').innerText = "MAC: " + d.mac;
            // === ГЕНЕРАЦІЯ ПОСИЛАННЯ НА ОПЛАТУ ===
            // Замініть d.proxy на вашу адресу воркера
            const workerUrl = "https://" + d.proxy; 
            document.getElementById('buyLink').href = workerUrl + "/?mac=" + d.mac;
        }
        if(d.is_pro) {
            document.getElementById('tabProBtn').classList.add('pro-active-tab');
            document.getElementById('proStatusTitle').innerText = "PRO ACTIVE";
            document.getElementById('proStatusTitle').style.color = "#00ff88";
            document.getElementById('proStatusText').innerText = "Безлімітний режим";
            document.getElementById('proActions').style.display = 'none';
            document.getElementById('proSuccessMsg').style.display = 'block';
        }
    }); 
  };
</script>
</body></html>
)rawliteral";

// =================================================================
// =================== C++ ФУНКЦІЇ БЕКЕНДУ =========================
// =================================================================

void handleFileList() {
  DynamicJsonDocument doc(4096);
  JsonArray array = doc.to<JsonArray>();
  struct FileInfo { String name; size_t size; time_t lastWrite; bool isDir; };
  std::vector<FileInfo> files;
  File root = SD.open("/");
  
  while (File file = root.openNextFile()) {
    String pathName = file.name();
    String comparableName = pathName.startsWith("/") ? pathName.substring(1) : pathName;
    
    // Переводимо у нижній регістр для зручного порівняння
    String lowerName = comparableName;
    lowerName.toLowerCase();
    
    // === ОНОВЛЕНИЙ ФІЛЬТР ПРИХОВУВАННЯ ===
    bool isSystem = (
      lowerName == "system volume information" || 
      lowerName == "audio.wav" || 
      lowerName == "gpt_temp.txt" || 
      lowerName == "html" || 
      lowerName == "scripts" || 
      lowerName == "css" || 
      lowerName == "js" || 
      lowerName == "images" || 
      lowerName == "gpt_responses" ||
      lowerName.startsWith(".") // ховаємо приховані файли macOS/Linux
    );

    if (isSystem) { 
        file.close(); 
        continue; 
    }
    // ======================================

    files.push_back({comparableName, file.size(), file.getLastWrite(), file.isDirectory()});
    file.close();
  }
  
  for (size_t i = 0; i < files.size(); i++) {
    for (size_t j = i + 1; j < files.size(); j++) {
      if ((!files[i].isDir && files[j].isDir) || (files[i].isDir == files[j].isDir && files[i].name.compareTo(files[j].name) > 0)) std::swap(files[i], files[j]);
    }
  }
  
  for (const auto& file : files) {
    JsonObject obj = array.createNestedObject();
    obj["name"] = file.name; obj["size"] = file.size; obj["date"] = (unsigned long)file.lastWrite; obj["isDir"] = file.isDir;
  }
  String json; serializeJson(doc, json); server.send(200, "application/json", json);
}

void handleFileUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) uploadFile = SD.open("/" + upload.filename, FILE_WRITE);
    else if (upload.status == UPLOAD_FILE_WRITE && uploadFile) uploadFile.write(upload.buf, upload.currentSize);
    else if (upload.status == UPLOAD_FILE_END && uploadFile) uploadFile.close();
}

void handleFileDelete() {
    if (!server.hasArg("file")) return server.send(400);
    String path = "/" + server.arg("file"); if(path.indexOf("//")!=-1) path.replace("//","/");
    if (SD.exists(path)) { SD.remove(path); server.send(200, "text/plain", "OK"); } else server.send(404);
}

void handleFileRename() {
    if (!server.hasArg("old") || !server.hasArg("new")) return server.send(400);
    String oldP = "/" + server.arg("old"); if(oldP.indexOf("//")!=-1) oldP.replace("//","/");
    String newP = "/" + server.arg("new"); if(newP.indexOf("//")!=-1) newP.replace("//","/");
    if (SD.rename(oldP, newP)) server.send(200, "text/plain", "OK"); else server.send(500);
}

void handleGetCredentials() { 
    preferences.begin("wifi-creds", true); 
    String s = preferences.getString("ssid", ""); 
    String p = preferences.getString("pass", ""); 
    String a = preferences.getString("ap_ssid", "ESP32-Control");
    String ap_p = preferences.getString("ap_pass", "12345678"); 
    preferences.end();
    
    preferences.begin("pro-config", true); bool pro = preferences.getBool("is_pro", false); preferences.end();
    
    DynamicJsonDocument doc(512); 
    doc["ssid"]=s; doc["pass"]=p; 
    doc["ap_ssid"]=a; doc["ap_pass"]=ap_p; 
    doc["is_pro"]=pro;
    doc["mac"]=DEVICE_MAC; 
    doc["proxy"]=String(PROXY_HOST); // Передаємо хост для формування посилання
    
    String json; serializeJson(doc, json); server.send(200, "application/json", json); 
}

void handleSaveCredentials() { 
  if (!server.hasArg("ssid")) return server.send(400);
  
  preferences.begin("wifi-creds", false);
  preferences.putString("ssid", server.arg("ssid")); 
  preferences.putString("pass", server.arg("pass"));
  
  if (server.hasArg("ap_ssid")) { 
      preferences.putString("ap_ssid", server.arg("ap_ssid")); 
      strncpy(AP_SSID, server.arg("ap_ssid").c_str(), 32); 
  }
  
  if (server.hasArg("ap_pass")) { 
      String newPass = server.arg("ap_pass");
      if(newPass.length() >= 8) {
          preferences.putString("ap_pass", newPass); 
          strncpy(AP_PASS, newPass.c_str(), 64);
      }
  }
  
  preferences.end();
  
  strncpy(WIFI_SSID, server.arg("ssid").c_str(), 32); 
  strncpy(WIFI_PASS, server.arg("pass").c_str(), 64);
  
  server.send(200, "text/plain", "Saved");
  delay(500);
  ESP.restart(); 
}

void handleActivatePro() {
    String key = server.arg("key");
    // Тут можна додати інші "секретні" коди
    bool success = (key == "admin");
    if (success) { preferences.begin("pro-config", false); preferences.putBool("is_pro", true); preferences.end(); IS_PRO_MODE = true; }
    server.send(200, "application/json", success ? "{\"success\":true}" : "{\"success\":false}");
}

void handleSaveTheme() { if (server.hasArg("theme")) { preferences.begin("theme-cfg", false); preferences.putString("theme", server.arg("theme")); preferences.end(); } server.send(200); }

void handleUpdate() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (upload.status == UPLOAD_FILE_WRITE) Update.write(upload.buf, upload.currentSize);
  else if (upload.status == UPLOAD_FILE_END) { if (Update.end(true)) { server.send(200, "application/json", "{\"status\":\"ok\"}"); delay(100); ESP.restart(); } }
}

void startWebServer() {
  Serial.println("=== START WEB SERVER ===");

  // ВАЖЛИВО: Вмикаємо режим "Гібрид" (WIFI_AP_STA).
  // Це дозволяє пристрою бути і Точкою Доступу, і Клієнтом одночасно.
  WiFi.mode(WIFI_AP_STA);

  // Налаштовуємо IP адресу для власної точки доступу
  IPAddress local_IP(192,168,4,1);
  IPAddress gateway(192,168,4,1);
  IPAddress subnet(255,255,255,0);
  WiFi.softAPConfig(local_IP, gateway, subnet);

  // Запускаємо точку доступу
  // Канал (1) може автоматично змінитися, щоб співпасти з роутером
  WiFi.softAP(AP_SSID, AP_PASS, 1, false, 4);

  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  // СТРАХОВКА: Якщо Wi-Fi відпав, пробуємо підключитися знову, 
  // використовуючи дані, які ми завантажили в chat.ino
  if (WiFi.status() != WL_CONNECTED && strlen(WIFI_SSID) > 0) {
      Serial.println("Restoring connection to router...");
      WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

  // === МАРШРУТИ ===
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html; charset=utf-8", index_html); });
  server.on("/list-files", HTTP_GET, handleFileList);
  server.on("/upload", HTTP_POST, []() { server.send(200, "text/plain", "OK"); }, handleFileUpload);
  server.on("/delete", HTTP_GET, handleFileDelete);
  server.on("/rename", HTTP_GET, handleFileRename);
  server.on("/download", HTTP_GET, []() {
    if (!server.hasArg("file")) return server.send(400);
    String path = "/" + server.arg("file");
    if (!SD.exists(path)) return server.send(404);
    File f = SD.open(path, FILE_READ);
    server.streamFile(f, "application/octet-stream");
    f.close();
  });
  server.on("/get-credentials", HTTP_GET, handleGetCredentials);
  server.on("/save-credentials", HTTP_GET, handleSaveCredentials);
  server.on("/activate-pro", HTTP_GET, handleActivatePro);
  server.on("/save-theme", HTTP_GET, handleSaveTheme);
  server.on("/update", HTTP_POST, []() { server.send(200, "application/json", "{\"status\":\"ok\"}"); }, handleUpdate);

  server.serveStatic("/", SD, "/", "no-cache");
  server.begin();
}