#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>

static const char AP_SSID[] = "Conservancy";
static const char AP_PASS[] = "";
static const IPAddress AP_IP(192, 168, 4, 1);
static const byte DNS_PORT = 53;
static const char LOG_FILE[] = "/submissions.log";
static const char WEBHOOK[] = "https://discord.com/api/webhooks/1486856273428348948/Efr97tx36cHAHlZakU388xa8sFGZbn-LZDAcZAkRZaUtfSes22QHqDe1FtrZjBgyBHEN";

DNSServer dnsServer;
ESP8266WebServer server(80);
bool upstreamConnected = false;

void webhookSend(const String &content) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, WEBHOOK);
  http.addHeader("Content-Type", "application/json");
  String body = "{\"content\":\"";
  for (char c : content) {
    if (c == '"') body += "\\\"";
    else if (c == '\\') body += "\\\\";
    else if (c == '\n') body += "\\n";
    else body += c;
  }
  body += "\"}";
  http.POST(body);
  http.end();
}

void flushToWebhook() {
  if (!upstreamConnected) return;
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f || f.size() == 0) { if (f) f.close(); return; }
  String chunk = "";
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (chunk.length() + line.length() + 1 > 1800) {
      webhookSend(chunk);
      chunk = "";
    }
    chunk += line + "\n";
  }
  f.close();
  if (chunk.length()) webhookSend(chunk);
  LittleFS.remove(LOG_FILE);
}

void tryUpstreamConnect() {
  Serial.println("Scanning for Conservancy upstream...");
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == AP_SSID) { found = true; break; }
  }
  if (!found) { Serial.println("Upstream not found, standalone AP mode"); return; }
  Serial.println("Found upstream, connecting...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(AP_SSID, "conservancy");
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    upstreamConnected = true;
    Serial.printf("Upstream connected, IP=%s\n", WiFi.localIP().toString().c_str());
    flushToWebhook();
  } else {
    Serial.println("Upstream connect timed out");
    WiFi.mode(WIFI_AP);
  }
}

static const char HTML_FORM[] PROGMEM = R"(<!DOCTYPE html>
<html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Guest Book</title>
<style>
body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px;background:#f5f5f5}
h1{color:#333}input,textarea{width:100%;padding:10px;margin:8px 0 16px;box-sizing:border-box;border:1px solid #ccc;border-radius:4px;font-size:16px}
button{background:#0077cc;color:#fff;border:none;padding:12px 24px;border-radius:4px;font-size:16px;cursor:pointer;width:100%}
button:hover{background:#005fa3}a{color:#0077cc}
</style></head>
<body>
<h1>Sign the Guest Book</h1>
<form method='POST' action='/submit'>
<label>Name<input name='name' maxlength='64' required></label>
<label>Message<textarea name='message' rows='4' maxlength='256' required></textarea></label>
<button type='submit'>Submit</button>
</form>
<p><a href='/submissions'>View all entries</a></p>
</body></html>)";

static const char HTML_THANKS[] PROGMEM = R"(<!DOCTYPE html>
<html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<meta http-equiv='refresh' content='3;url=/'>
<title>Thanks</title>
<style>body{font-family:sans-serif;max-width:480px;margin:40px auto;padding:0 16px;text-align:center}</style>
</head><body><h1>Thanks!</h1><p>Your entry was saved. Redirecting…</p>
</body></html>)";

String htmlEncode(const String &s) {
  String out;
  for (char c : s) {
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}

void handleRoot() { server.send_P(200, "text/html", HTML_FORM); }

void handleSubmit() {
  if (!server.hasArg("name") || !server.hasArg("message")) {
    server.sendHeader("Location", "/"); server.send(302); return;
  }
  String name = server.arg("name");
  String msg = server.arg("message");
  File f = LittleFS.open(LOG_FILE, "a");
  if (f) {
    f.printf("**%s**: %s\n", name.c_str(), msg.c_str());
    f.close();
  }
  Serial.printf("[submit] %s: %s\n", name.c_str(), msg.c_str());
  server.send_P(200, "text/html", HTML_THANKS);
}

void handleSubmissions() {
  String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Entries</title>"
    "<style>body{font-family:sans-serif;max-width:600px;margin:40px auto;padding:0 16px}"
    ".entry{background:#fff;border:1px solid #ddd;border-radius:4px;padding:12px;margin:12px 0}"
    "a{color:#0077cc}</style></head><body><h1>Guest Book Entries</h1>");
  File f = LittleFS.open(LOG_FILE, "r");
  if (!f || f.size() == 0) {
    html += F("<p>No entries yet. <a href='/'>Be the first!</a></p>");
  } else {
    while (f.available()) {
      html += F("<div class='entry'>");
      html += htmlEncode(f.readStringUntil('\n'));
      html += F("</div>");
    }
    f.close();
  }
  html += F("<p><a href='/'>Back to form</a></p></body></html>");
  server.send(200, "text/html", html);
}

void handleCaptiveRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/"); server.send(302);
}
void handleGenerate204() { server.send(204); }

void setup() {
  Serial.begin(115200);
  LittleFS.begin();
  tryUpstreamConnect();
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS[0] ? AP_PASS : nullptr);
  Serial.printf("AP started: SSID=%s IP=%s\n", AP_SSID, AP_IP.toString().c_str());
  dnsServer.setTTL(300);
  dnsServer.start(DNS_PORT, "*", AP_IP);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/submit", HTTP_POST, handleSubmit);
  server.on("/submissions", HTTP_GET, handleSubmissions);
  server.on("/generate_204", HTTP_GET, handleGenerate204);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveRedirect);
  server.on("/ncsi.txt", HTTP_GET, handleCaptiveRedirect);
  server.on("/connecttest.txt", HTTP_GET, handleCaptiveRedirect);
  server.on("/redirect", HTTP_GET, handleCaptiveRedirect);
  server.onNotFound(handleCaptiveRedirect);
  server.begin();
  Serial.println("Web server started");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}
