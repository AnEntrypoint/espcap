#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

static const char AP_SSID[] = "Conservancy";
static const char AP_PASS[] = "";
static const IPAddress AP_IP(192, 168, 4, 1);
static const byte DNS_PORT = 53;
static const uint8_t MAX_ENTRIES = 50;

struct Entry {
  String name;
  String message;
  unsigned long ms;
};

DNSServer dnsServer;
ESP8266WebServer server(80);
Entry entries[MAX_ENTRIES];
uint8_t entryCount = 0;

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
</head><body>
<h1>Thanks!</h1><p>Your entry was saved. Redirecting…</p>
</body></html>)";

String htmlEncode(const String &s) {
  String out;
  out.reserve(s.length());
  for (char c : s) {
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
  return out;
}

void handleRoot() {
  server.send_P(200, "text/html", HTML_FORM);
}

void handleSubmit() {
  if (!server.hasArg("name") || !server.hasArg("message")) {
    server.sendHeader("Location", "/");
    server.send(302);
    return;
  }
  if (entryCount < MAX_ENTRIES) {
    entries[entryCount++] = {server.arg("name"), server.arg("message"), millis()};
  }
  Serial.printf("[submit] %s: %s\n", server.arg("name").c_str(), server.arg("message").c_str());
  server.send_P(200, "text/html", HTML_THANKS);
}

void handleSubmissions() {
  String html = F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Entries</title>"
    "<style>body{font-family:sans-serif;max-width:600px;margin:40px auto;padding:0 16px}"
    ".entry{background:#fff;border:1px solid #ddd;border-radius:4px;padding:12px;margin:12px 0}"
    ".name{font-weight:bold}.time{color:#999;font-size:12px}a{color:#0077cc}</style></head><body>"
    "<h1>Guest Book Entries</h1>");
  if (entryCount == 0) {
    html += F("<p>No entries yet. <a href='/'>Be the first!</a></p>");
  } else {
    for (int i = entryCount - 1; i >= 0; i--) {
      html += F("<div class='entry'><div class='name'>");
      html += htmlEncode(entries[i].name);
      html += F("</div><div class='msg'>");
      html += htmlEncode(entries[i].message);
      html += F("</div><div class='time'>+");
      html += String(entries[i].ms / 1000);
      html += F("s uptime</div></div>");
    }
  }
  html += F("<p><a href='/'>Back to form</a></p></body></html>");
  server.send(200, "text/html", html);
}

void handleCaptiveRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/");
  server.send(302);
}

void handleGenerate204() {
  server.send(204);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS[0] ? AP_PASS : nullptr);
  Serial.printf("\nAP started: SSID=%s IP=%s\n", AP_SSID, AP_IP.toString().c_str());

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
