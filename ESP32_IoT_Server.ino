/*********
ESP32_IoT_Server
Mark Poehlman 10/4/2018
ESP32 Dev Board Web Server with .96" OLED and Thermistor Temperature Sensor
Submit form to pass message to OLED
Submit valid command to control on board LED
Monitor switch on Pin D26
*********/

//https://github.com/espressif/arduino-esp32/tree/master/libraries
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Arduino.h>  
#include <U8x8lib.h>  // https://github.com/olikraus/u8g2
#include <SPI.h>

// Replace with your network credentials
const char* ssid = "YourSSID";
const char* password = "SSIDpassword";
String on_command = "ledon"; //Change to your valid command
String off_command = "ledoff"; //Change to your valid command
String webPage = "";
int led = 2; //Onboard LED, connect D2 to relay to control other things.
int switch1 = 26; //Input pin
String led_status = "OFF";
String input_pins;
int loop_ctr = 0;
WebServer server(80);

//*********************** Setup OLED Display ***************************
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(/* clock=*/ SCL, /* data=*/ SDA, /* reset=*/ U8X8_PIN_NONE);   // OLED Display
// OLED Wiring
// SCL to ESP32 Pin D22
// SDA to ESP32 Pin D21
//************** NTC Thermistor Configuration **************************
#define THERMISTORPIN 34   //Which analog pin to connect   +<thermistor>D34<10k Ohm>Gnd  
#define THERMISTORNOMINAL 8200 // resistance at 25 degrees C  
#define TEMPERATURENOMINAL 25   // temp. for nominal resistance (almost always 25 C)
#define NUMSAMPLES 5 //how many samples to take and average, more takes longer but is more 'smooth'
#define BCOEFFICIENT 3450 //The beta coefficient of the thermistor (usually 3000-4000)
#define SERIESRESISTOR 10000   //Value of the series resistor 
uint16_t samples[NUMSAMPLES];
float temperature;

//********************* Setup ***************************************
void setup(void){
  u8x8.begin();
  u8x8.setPowerSave(0);
  
  // preparing GPIOs
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);
  pinMode(switch1, INPUT_PULLUP); //Switch1 will always read high if left disconnected because of pullup resistor
  
  
  Serial.begin(115200); 
  delay(10);
  WiFi.begin(ssid, password);
  Serial.println("");
  Serial.print("Sketch File Name: ");
  Serial.println(F(__FILE__ " " __DATE__ " " __TIME__));
  u8x8.clear();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.drawString(0,0,"Connecting:");
  u8x8.drawString(0,1,ssid);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
//Display IP on OLED ----------------------
  char buf1[24];
  sprintf(buf1, "%d.%d.%d.%d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3] );//Display complete IP Address
  u8x8.clear();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.drawString(0,0,buf1);//print ip address on display line 0
  
  u8x8.drawString(0,6,"LED=OFF");//Display initial Onboard LED value

// Server Actions ------------------
   server.on("/", handleRoot);
   

  server.begin();
  Serial.println("HTTP server started");

  //--- Initial Web Page Build ---
  read_input_pins();
  temperature = get_temp();
  build_page();  //Build the web page initially
}
//**************************************** loop ************************************** 
void loop(void){
 
  server.handleClient(); //Handle Web Client
  read_input_pins(); //Check status of Switch1
  
  loop_ctr ++;  //Count loops. This limits time spent reading temperature which is a slow process.
  if (loop_ctr > 100000) {   //read temp periodically, ~1 second at 100000
      temperature = get_temp(); //Get temperature occasionally
      loop_ctr = 0;} 
}

//************************************** build page ********************************************
void build_page()
{
  webPage = "";
  //Create simple web page with one submit form
  webPage += "<h1>ESP32 IoT Web Server</h1><p><br>";  
  webPage += "<text>Submit valid command to control onboard LED.<text><br>";  
  webPage += "<form method=post>"; 
  webPage += "<input type=text name=data maxlength=16>";//data present when submit is clicked, can be null, max = 16 characters.
  webPage += "<input type=submit value=Submit>";
  webPage += "<br><br>LED Status: "+String(led_status);  //Variable led_status
  webPage += "<br><br>Input Pins: "+String(input_pins);  //String input_pins
  webPage += "<br><br>Temperature ('F): "+String(temperature); //Temperature
  webPage += "</form></p>";
}

//************************************** handle root ********************************************
void handleRoot()
{
  if (server.hasArg("data")) { //Has data been submitted?
     handleSubmit();
  }
  else {
    server.send(200, "text/html", webPage);
  }
}

//*************************************** handle submit *****************************************
void handleSubmit()
{
  String value = "";
  char valbuf[17];//valbuf will be displayed on OLED
  value = server.arg("data");//this is the data submitted from the webpage form
  value.toCharArray(valbuf, 18); //
  u8x8.drawString(0,2,"                ");// Clear the line
  u8x8.drawString(0,2,valbuf);//output text to line2 starting at position 0
    
  if (value == on_command){
          digitalWrite(led, HIGH);
          u8x8.drawString(0,6,"                ");// Clear the line
          u8x8.drawString(0,6,"LED=ON ");
          led_status = "ON";
  }

  if (value == off_command){
         digitalWrite(led, LOW);
         u8x8.drawString(0,6,"                ");// Clear the line
         u8x8.drawString(0,6,"LED=OFF");
         led_status = "OFF";
  }
   
  build_page(); //Update web page with current led_status
  server.send(200, "text/html", webPage);

}

//************************* Read Input Pins *****************************
void read_input_pins(){
  if (digitalRead(switch1) == HIGH){     // read the input pin
  input_pins = "Switch1 is High"; }
  if (digitalRead(switch1) == LOW){
  input_pins = "Switch1 is Low";  }
  
}

//********** Get Temperature using the Steinhart Hart Equation ************
float get_temp(void) {
  uint8_t i;
  float average;
  char temp_array[16];
 
  // take N samples in a row, with a slight delay
  for (i=0; i< NUMSAMPLES; i++) {
   samples[i] = analogRead(THERMISTORPIN);
   delay(10);
  }
 
  // average all the samples out
  average = 0;
  for (i=0; i< NUMSAMPLES; i++) {
     average += samples[i];
  }
  average /= NUMSAMPLES;
 
  // convert the value to resistance 
  average = 4095 / average - 1; //4095 is maximum A/D converter value
  average = SERIESRESISTOR / average;
 
  float steinhart;
  steinhart = average / THERMISTORNOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert to C
  float fahrenheit = (1.8*steinhart) + 32;    //Convert to 'F

  sprintf(temp_array, "Temp=%3.1f", fahrenheit);//3.1 means 3 digits left of decimal and 1 to the right
  u8x8.drawString(0,4,"                ");// Clear the line
  u8x8.drawString(0,4,temp_array);
  return fahrenheit;
  
}
