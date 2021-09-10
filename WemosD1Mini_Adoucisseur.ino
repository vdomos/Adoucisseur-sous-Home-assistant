/*
 * Wemos D1 Mini - adoucisseur CR2J FMB 20 - MQTT - OTA

 * 7/9/2021
 
*/

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>     // https://pubsubclient.knolleary.net/api.html
#include <ArduinoOTA.h>
#include <Adafruit_ADS1X15.h>

#define NAME "Wemosd1mini1 - adoucisseur"
#define VERSION __DATE__ " " __TIME__

// Update these with values suitable for your network.
const char* ssid = "<SSID>";
const char* password = "xxxxxxxxxxxxxxxxxx";

// Nom d'hôte OTA (pour mDNS)
const char* otahostname = "wemosd1mini1";
// mot de passe pour l'OTA
const char* otapass = "123456";             // Code pour flasher
// gestion du temps pour calcul de la durée de la MaJ
unsigned long otamillis;

const char* mqtt_server = "brocker.local";  // nom de la machine ayant le broker (mDNS)
const char* mqtt_user = "brocker_user";
const char* mqtt_pwd = "brocker_pwd";

WiFiClient espClient;

PubSubClient mqttClient(espClient);
long lastMillis = -100000;

Adafruit_ADS1115 ads;     /* Use this for the 16-bit version */
int16_t adc0, adc1;
float volts ;
long niveausel;
char gp[5];

// ATTENTION: MQTT_MAX_PACKET_SIZE passé de 128 à 512 dans "PubSubClient.h", sinon message de config ne passe pas.
#define TOPICSTATUS "sensor/esp-wemosd1mini1_adoucisseur/status"

// Voir signification des "Supported abbreviations" sur https://www.home-assistant.io/docs/mqtt/discovery
#define HATOPICCONFIG1 "homeassistant/sensor/esp-wemosd1mini1_adoucisseur/niveausel/config"  
#define HAPAYLOADCONFIG1 "{\"avty\":\
[{\"topic\": \"sensor/esp-wemosd1mini1_adoucisseur/status\"}],\
\"device\": {\
\"ids\": [\"adoucisseur\"],\
\"mf\": \"Domos\",\
\"mdl\": \"Wemosd1mini1 adoucisseur\",\
\"name\": \"Adoucisseur\",\
\"sw\": \""VERSION"\"\
},\
\"name\": \"Adoucisseur niveausel\",\
\"stat_t\": \"sensor/esp-wemosd1mini1_adoucisseur/niveausel\",\
\"uniq_id\": \"esp-wemosd1mini1_adoucisseur/niveausel\",\
\"frc_upd\": true,\
\"ic\": \"mdi:car-coolant-level\",\
\"unit_of_meas\": \"%\",\
\"exp_aft\": 300\
}"

#define HATOPICCONFIG2 "homeassistant/binary_sensor/esp-wemosd1mini1_adoucisseur/state/config"  
#define HAPAYLOADCONFIG2 "{\"avty\":\
[{\"topic\": \"sensor/esp-wemosd1mini1_adoucisseur/status\"}],\
\"device\": {\
\"ids\": [\"adoucisseur\"],\
\"mf\": \"Domos\",\
\"mdl\": \"Wemosd1mini1 adoucisseur\",\
\"name\": \"Adoucisseur\",\
\"sw\": \""VERSION"\"\
},\
\"name\": \"Adoucisseur régénération\",\
\"stat_t\": \"sensor/esp-wemosd1mini1_adoucisseur/state\",\
\"uniq_id\": \"esp-wemosd1mini1_adoucisseur/state\",\
\"frc_upd\": true,\
\"pl_on\": \"on\",\ 
\"pl_off\": \"off\",\
\"exp_aft\": 300\
}"


// ----------------------------------------------------------------------------
void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // Désactive mode AP avant connexion
  WiFi.softAPdisconnect(true);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}


// ----------------------------------------------------------------------------
void confOTA() {
  // Port 8266 (défaut)
  ArduinoOTA.setPort(8266);

  // Hostname défaut : esp8266-[ChipID]
  ArduinoOTA.setHostname(otahostname);

  // mot de passe pour OTA
  ArduinoOTA.setPassword(otapass);

  // lancé au début de la MaJ
  ArduinoOTA.onStart([]() {
    Serial.println("/!\\ OTA update");
    otamillis=millis();
  });

  // lancé en fin MaJ
  ArduinoOTA.onEnd([]() {
    delay(0.1);
    Serial.print("\n/!\\ Update ended ");
    Serial.print((millis()-otamillis)/1000.0);
    Serial.println(" secondes");
  });

  // lancé lors de la progression de la MaJ
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    delay(0.1);
    Serial.printf("Progression: %u%%\r", (progress / (total / 100)));
  });

  // En cas d'erreur
  ArduinoOTA.onError([](ota_error_t error) {
    delay(0.1);
    Serial.printf("Error[%u]: ", error);
    switch(error) {
      // erreur d'authentification, mauvais mot de passe OTA
      case OTA_AUTH_ERROR:     Serial.println("OTA_AUTH_ERROR");
                               break;
      // erreur lors du démarrage de la MaJ (flash insuffisante)
      case OTA_BEGIN_ERROR:    Serial.println("OTA_BEGIN_ERROR");
                               break;
      // impossible de se connecter à l'IDE Arduino
      case OTA_CONNECT_ERROR:  Serial.println("OTA_CONNECT_ERROR");
                               break;
      // Erreur de réception des données
      case OTA_RECEIVE_ERROR:  Serial.println("OTA_RECEIVE_ERROR");
                               break;
      // Erreur lors de la confirmation de MaJ
      case OTA_END_ERROR:      Serial.println("OTA_END_ERROR");
                               break;
      // Erreur inconnue
      default:                 Serial.println("OTA unknow error");
    }
  });

  // Activation fonctionnalité OTA
  ArduinoOTA.begin();

  Serial.println("OTA Configuration done");
}


/*------------------------------------------------------------------------------*/
/* Fonctions MQTT                                                          */
/*------------------------------------------------------------------------------*/
void mqttReconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection ... ");
    // Create a random client ID
    String clientId = "wemosd1mini1-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pwd) {     // https://github.com/Hackable-magazine/Hackable26/blob/master/espmqtt_auth/espmqtt_auth.ino
      Serial.println("connected");
      mqttClient.publish(HATOPICCONFIG1, HAPAYLOADCONFIG1, true);       // avec retain
      delay(500);
      mqttClient.publish(HATOPICCONFIG2, HAPAYLOADCONFIG2, true);       // avec retain
      delay(500);
      mqttClient.publish(TOPICSTATUS, "online", true);                  // avec retain
      mqttClient.publish("sensor/esp-wemosd1mini1_adoucisseur/name", NAME);
      mqttClient.publish("sensor/esp-wemosd1mini1_adoucisseur/version", VERSION);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}


// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void setup() {
    delay(500);
    Serial.begin(115200);
    Serial.println(F("\nSetup ..."));
    Serial.print(NAME);
    Serial.print(F(" - "));
    Serial.println(VERSION);

    pinMode(BUILTIN_LED, OUTPUT);         // Initialize the BUILTIN_LED pin as an output
    digitalWrite(BUILTIN_LED, LOW);       // Turn the LED on
  
    setup_wifi();
    delay(500);
  
    // Init MQTT
    mqttClient.setServer(mqtt_server, 1883);
    delay(500);

    // ADS1115
    ads.begin();

    // OTA
    confOTA();                                    // configuration OTA
    digitalWrite(BUILTIN_LED, HIGH);              // Turn the LED off    
    delay(500);

    Serial.println("Setup ended\n");
}


// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
void loop() {
    if (!mqttClient.connected()) {
        mqttReconnect();
    }
    mqttClient.loop();


    if (millis() - lastMillis > 100000) {
        // Niveau sel (GP)
        adc0 = ads.readADC_SingleEnded(0);              // Niveau sel (GP)
        volts = ads.computeVolts(adc0);                 // Tension GP    (tension baisse avec le niveau de sel, 2.39V max pour 2 sac de sel)
        Serial.print("AIN0: "); Serial.print(adc0); Serial.print("  "); Serial.print(volts); Serial.println("V");
        niveausel = (volts - 1.12) / 1.27 * 100 ;       // min: 1.12 .. max: 2.39  =>  0..1.27 =>  0..100%
        dtostrf(niveausel, 5, 1, gp);
        mqttClient.publish("sensor/esp-wemosd1mini1_adoucisseur/niveausel", gp);    

        // Témoin rouge (LDR)
        adc1 = ads.readADC_SingleEnded(1);              // Témoin rouge (LDR)
        volts = ads.computeVolts(adc1);                 // Tension LDR
        Serial.print("AIN1: "); Serial.print(adc1); Serial.print("  "); Serial.print(volts); Serial.println("V");
        if (volts > 2.5) {
            mqttClient.publish("sensor/esp-wemosd1mini1_adoucisseur/state", "on");
        }
        else {
            mqttClient.publish("sensor/esp-wemosd1mini1_adoucisseur/state", "off");
        };
      
        lastMillis = millis();
    }

    ArduinoOTA.handle();                                // gestion OTA
}
