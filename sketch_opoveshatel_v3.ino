#include <ArduinoJson.h>       //библиотека для работы с файлами формата JSON
#include <ESP8266WiFi.h>       //библиотека для работы с WiFi
#include <ESP8266HTTPClient.h> //библиотека для работы с сервером
#include <SPI.h>               //библиотека для работы с sd картой
#include <SD.h>                //библиотека для работы с sd картой
#include <Wire.h>              //библиотека для связи микроконтроллера ESP8266 d1 mini с устройствами и модулями через интерфейс I2C
#include <Adafruit_GFX.h>      //библиотека для работы с oled экраном
#include <Adafruit_SSD1306.h>  //библиотека для работы с oled экраном
#include <RTClib.h>            //библиотека для работы c DS3231 (модулем реального времени)
#include <AudioFileSourceSD.h> //библиотека для работы с sd картой (указание источника аудио файла)
#include <AudioFileSourceID3.h>//Это класс, который принимает в качестве входных данных любой другой источник аудиофайла и выводит источник аудиофайла
                               //подходящий для любого декодера.
#include "AudioGeneratorMP3.h" //библиотека для работы с аудио файлами mp3 формата. Считывает и воспроизводит файлы формата MP3 (.MP3) с использованием портированной библиотеки libMAD
#include "AudioOutputI2S.h"    //библиотека для вывода аудио по протоколу I2S. Интерфейс для любого 16-разрядного ЦАП I2S. Посылает стерео- или моно-сигналы на любой установленной частоте.

//===================== Данные для подключения к WiFi =====================//
const char* ssid = "YourSSID";        //имя сети
const char* password = "YourPASSWORD"; //пароль сети
//===================== ============================= =====================//

//===================== Данные для работы с sd картой =====================//
const int bufferSize = 512;
const int bufferSizeServ = 512;
//===================== ============================= =====================//

//===================== Данные работы с аудио файлами =====================//
static bool played = false;
static bool initAud = false;
static bool onetime = true;
AudioGeneratorMP3 *mp3;
AudioFileSourceSD *file;
AudioOutputI2S *out;
AudioFileSourceID3 *id3;
//===================== ============================= =====================//

//===================================================================== Данные для подключения к Серверу =====================================================================//
IPAddress serverIP(*, *, *, *); //это экземпляр класса `IPAddress`, который мы создали на основе строки "*.*.*.*", его будем использовать для обращения к серверу
const uint16_t serverPort = 80; // Порт 80 (TCP) используется для передачи контента клиентам по запросу
const char* apiEndpoint = "/download/jsons/445.json"; // - ссылка для обращения к серверу сайта, получение json файла
const unsigned long interval = 300000; // интервал времени в 30 секунд, каждые 30 секунд опрашиваем сервер (проверка json и аудио)
unsigned long nextCheck = 0; // переменная, необходимая, чтобы сделать счётчик в связке с переменной interval
//===================== ================================================================================================================================ =====================//

//===================== Объявление для SSD1306 дисплея использующуего подключение I2C =====================//
#define SCREEN_WIDTH 128 // Длина дисплея в пикселях
#define SCREEN_HEIGHT 32 // Ширина дисплея в пикселях
#define OLED_RESET -1 // Reset pin
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
//===================== ===================== ================= ===================== =====================//

//===================== Объявление для часов реального времени DS3231 =====================//
RTC_DS3231 rtc;
String closestTime;
String fileType, filename;
int lastMinute = -1;
//===================== ============================================= =====================//

//=========================== Объявление для JSON и аудио =============================//
const String filePath = "/data.json"; //путь, где храним json файл, скачанный с сервера
const String nameOfDir = "/sounds"; //путь, где храним музыку
//===================== ========================================= =====================//

//=================================== Функция инициализации WiFi ===================================//
void initWiFi() {                           //пока не подключились - пробуем подключиться к сети
  WiFi.mode(WIFI_OFF);                      //используя данные переменных ssid и password
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);               
  while (WiFi.status() != WL_CONNECTED){ 
    delay(2000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
}
//===================== ====================================================== =====================//

//======================================= Функция инициализации SD карты =======================================//
void initSD() {
  //Подключение к SD карте
  Serial.println("Initializing SD card...");
  if (!SD.begin(D3)) {                      // инициализация по D3 (пину 0) Cheap Select, предназначенному
   Serial.println("initialization failed!");// в нашем случае для отклика и обмена информацией по шине SPI
   return;
  }
  Serial.println("initialization done.");
}
//===================== ================================================================== =====================//

//======================================= Функция инициализации Oled экрана =======================================//
void initOled() {
  // Инициализация объекта oled
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
  }else{
    Serial.println("Initialization Oled is successful");
  }
  display.display();
  delay(2000);
}
//===================== ==================================================================== =====================//

//======================================= Функция инициализации Oled экрана =======================================//
void initDS3231() {
  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    Serial.flush();
    while (1) delay(10);
  }else{
    Serial.println("Initialization DS3231 is successful");
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, let's set the time!");
    // Когда необходимо установить время на новом устройстве или после отключения питания,
    // следующая строка устанавливает RTC на дату и время, когда был составлен этот эскиз.
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  //rtc.adjust(DateTime(2023, 4, 28, 18, 2, 0));
}
//===================== ==================================================================== =====================//

void initAudio(const char* filename) {
  audioLogger = &Serial;
  file = new AudioFileSourceSD(filename);
  id3 = new AudioFileSourceID3(file);
  out = new AudioOutputI2S();
  mp3 = new AudioGeneratorMP3();
  mp3->begin(id3, out);
}

void playAudio() {
  if (mp3->isRunning()) {
    if (!mp3->loop()) mp3->stop();
  } else {
    Serial.printf("MP3 done\n");
    played = false;
    onetime = false;
    if(mp3){
      mp3->stop();
      delete mp3;
      mp3 = NULL;
    }
    if(id3) {
      id3->close();
      delete id3;
      id3 = NULL;
    }
    if(file){
      file->close();
      delete file;
      file = NULL;
    }
    delay(1000);
  }
}

//======================================= Функция вывода информации на Oled экран =======================================//
void InformationOutputOled(String currentTime, String closestTime, String filename, String fileType) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println(currentTime);
  display.setCursor(0,8);
  display.println(closestTime);
  display.setCursor(0,16);
  display.println(filename);
  display.setCursor(0,24);
  display.println(fileType);
  display.display();
  delay(2000);
}
//===================== ========================================================================== =====================//

//======================================= Функция получения тукущего времени и прочей информации =======================================//
/*
Сначала получаем реальное время, которое записываем в переменную currentTime в нужном нам формате, используя буфер типа char.
Дальше проверяем, равна ли текущая минута последней считанной минуте из модуля реального времени (при включении устройства изначальное значение последней минуты равно: -1,
чтобы первый раз выполнилось условие). Затем открываем JSON файл с sd карты, содержащий структуру с полями времени, чтобы определить ближайшее время воспроизведения оповещения.
Также, анализируя JSON файл забираем из него информацию о названии аудио и его типе. В конце отправляем полученную информацию на oled экран, используя функцию:
void InformationOutputOled(String currentTime, String closestTime, String filename, String fileType), принимающую в себя упомянутые раннее параметры. После проверяем,
равно ли текущее время времени воспроизведения нашего аудио (если да, то выставляем флаги на инициализацию и проигрывание аудио в положение true, чтобы воспроизведение
произошло. Ещё выставляется флаг "onetime", чтобы аудио воспроизводилось лишь 1 раз при наступлении времени) и обновляем значение последней минуты, 
чтобы наша функция выполняла описанный выше алгоритм только тогда, когда текущая минута (now.minute()) отличается от последней пройденной минуты (lastMinute).
*/
void dateComparison() {
  DateTime now = rtc.now();
  char buf1[] = "hh:mm";
  String currentTime = now.toString(buf1);
  String name;
  String playTime;
  String AudioType;
  if (now.minute() != lastMinute) {
    Serial.print("Current time: ");
    Serial.println(now.toString(buf1));
    File file = SD.open(filePath);
    if (file) {
      String content = file.readString();
      file.close();

      DynamicJsonDocument doc(2048);
      deserializeJson(doc, content);
      
      // Проходимся по каждому объекту структуры в Json файле
      for (JsonPair object : doc.as<JsonObject>()) {
        // Получаем ссылки и имена файлов для каждого объекта структуры Json
        name = object.key().c_str();
        playTime = object.value()["time"].as<String>();
        AudioType = object.value()["type"].as<String>();
        name.replace(":","");
        if (playTime >= currentTime) {
          closestTime = playTime;
          fileType = AudioType;
          filename = name;
          break;
        }else{
          closestTime = "No for today";
          fileType = "No for today";
          filename = "No for today";
        }
      }
      Serial.println("Closest time: " + closestTime);
      Serial.println("File name: " + filename);
      Serial.println("File type: " + fileType);
    }else {
      Serial.println("Failed to open file");
    }
    InformationOutputOled(currentTime, closestTime, filename, fileType);
    if(currentTime == closestTime){
      if(onetime){
        initAud = true;
        played = true;
      }else{
        initAud = false;
        played = false;
      }
    }
    if(currentTime != closestTime) {
     onetime = true;
    }
    lastMinute = now.minute();
  }
  delay(1000);
}
//===================== ========================================================================================== =====================//

//======================================= Функция получения файла формата JSON с сервера =======================================//
void getJsonFromServ() {
  //Если Json файл существует на sd карте, то скачиваем новый и сравниваем, оставляя или заменяя 
  //Если Json файла нет, то просто скачиваем новый
  if (SD.exists(filePath)) {
    File file = SD.open(filePath);
    if (file) {
      String content = file.readString();
      file.close();

      DynamicJsonDocument oldJson(2048);
      deserializeJson(oldJson, content);

      // Делаем HTTP запрос
      WiFiClient client;
      if (client.connect(serverIP, serverPort)) {
        client.println(String("GET ") + apiEndpoint + " HTTP/1.1\r\n" +
                     "Host: " + serverIP.toString() + "\r\n" +
                     "Connection: close\r\n\r\n");
        // Ждём ответ от сервера
        while(!client.available()) {
          delay(1);
        }
        while (client.connected())
          if (client.readStringUntil('\n') == "\r") break; // пропускаем хедер
        
        // Скачиваем новый Json файл
        DynamicJsonDocument newJson(2048);
        deserializeJson(newJson, client);

        if (newJson != oldJson) {
          Serial.println("Updating file");
          SD.remove(filePath);
          File file = SD.open(filePath, FILE_WRITE);
          if (file) {
            serializeJsonPretty(newJson, file);
            file.close();
          }
        } else {
          Serial.println("File is up-to-date");
        }
      } else {
        Serial.println("Failed to connect to server");
      }
      client.stop();
    } else {
      Serial.println("Failed to open file");
    }
  } else {
    Serial.println("File not found, creating...");
    WiFiClient client;
    if (client.connect(serverIP, serverPort)) {
      client.print(String("GET ") + apiEndpoint + " HTTP/1.1\r\n" +
                   "Host: " + serverIP.toString() + "\r\n" +
                   "Connection: close\r\n" + "\r\n");
      // Ждём ответ сервера
      while(!client.available()) {
        delay(1);
      }
      while (client.connected())
          if (client.readStringUntil('\n') == "\r") break; // пропускаем хедер

      // скачиваем новый Json
      DynamicJsonDocument newJson(2048);
      deserializeJson(newJson, client);

      File file = SD.open(filePath, FILE_WRITE);
      if (file) {
        serializeJsonPretty(newJson, file);
        file.close();
      }
    } else {
      Serial.println("Failed to connect to server");
    }
    client.stop();
  }
}
//===================== ================================================================================== =====================//

//======================================= Функция анализа файла JSON формата на sd карте =======================================//
/*
Промежуточная функция предназначеная для того, чтобы, проанализировав JSON файл с sd карты, получить имя файлов и ссылки, 
по которым к ним можно получить доступ. После получения нужных параметров, функция void parseJson() обращается к функции 
getSoundsFromServ(downloadUrl, filename) для дальнейших действий, о которых вы можете прочитать далее.
*/
void parseJson() {
  // Проверяем, есть ли директория и если нет, то создаем
  if (!SD.exists(nameOfDir)) {
    SD.mkdir(nameOfDir);
  }   
  //Открываем Json файл
  File file = SD.open(filePath);
  if (file) {
    String content = file.readString();
    file.close();

    DynamicJsonDocument doc(4096);
    deserializeJson(doc, content);
    // Проходимся по каждому объекту структуры в Json файле
    for (JsonPair object : doc.as<JsonObject>()) {
      // Получаем ссылки и имена файлов для каждого объекта структуры Json
      String downloadUrl = "http://*.*.*.*" + object.value()["files"].as<String>();
      String filename = String("/sounds/") + object.key().c_str() + ".mp3";
      filename.replace(":","");
      // Используем функцию скачивания мп3 для каждого объекта структуры Json
      getSoundsFromServ(downloadUrl, filename);
    }
  }else {
    Serial.println("Failed to open file");
  }
}
//===================== ================================================================================== =====================//

//======================================= Функция проверки на наличие и скачивания аудио файлов =======================================//
/*
Проверяем, существует ли файл с таким именем в каталоге /sounds, если да, то будем сравнивать его размер с размером нового аудио.
Если размеры отличаются, то старый файл заменяется новым, иначе оставляем старый файл. Если же файла с таким названием изначально не
существовало в каталоге, то просто скачиваем новый, размещая его в /sounds
*/
void getSoundsFromServ(String downloadUrl, String filename) {
  // Проверяем, есть ли файл в директории и если есть, то пропускаем скачивание
  if (SD.exists(filename)) {
    Serial.println("File already exists: " + filename);
    File file = SD.open(filename, FILE_READ);
    if (!file) {
      Serial.println("Ошибка открытия файла");
      return;
    }
    size_t oldFileSize = file.size();
    Serial.println(oldFileSize);
    file.close();

    File file2 = SD.open("/new" + filename, FILE_WRITE);
    if (!file2) {
      Serial.println("Ошибка открытия файла");
      return;
    }
    WiFiClient client;
    size_t newFileSize;
    if (client.connect(serverIP, serverPort)) {
      client.print(String("GET ") + downloadUrl + " HTTP/1.1\r\n" +
                     "Host: " + serverIP.toString() + "\r\n" +
                     "Connection: close\r\n\r\n");
      // Ждём ответ от сервера
      while (client.connected()) {
        if (client.available()) {
          byte buffer[bufferSizeServ];
          int bytesRead = client.read(buffer, bufferSizeServ);
          file2.write(buffer, bytesRead);
        }
      }
    newFileSize = file2.size();
    Serial.println(newFileSize);
    file2.close(); // Закрытие файла на SD-карте
    client.stop();
    }else {
      Serial.println("Failed to connect to server.");
      return;
    }
    Serial.println("Checking if we have any updates...");
    
    if(round(oldFileSize) != round(newFileSize)){
      SD.remove(filename);
      File file = SD.open("/new" + filename, FILE_READ);
      if (!file) {
        Serial.println("Ошибка открытия файла");
       return;
      }
      File newFile = SD.open(filename, FILE_WRITE);
      if (!newFile) {
        Serial.println("Ошибка открытия файла");
        return;
      }
      while(file.available()){
        byte buffer[bufferSize];
        int bytesRead = file.read(buffer, bufferSize);
        newFile.write(buffer, bytesRead);
      }
      file.close();
      newFile.close();
      Serial.println("Новый файл записан: " + filename);
    }
    SD.remove("/new" + filename);
    return;
  }else {
    Serial.println("Files are the same");
  }

  File file = SD.open(filename, FILE_WRITE); // Создание файла на SD-карте
  WiFiClient client;
  if (client.connect(serverIP, serverPort)) {
    client.print(String("GET ") + downloadUrl + " HTTP/1.1\r\n" +
                     "Host: " + serverIP.toString() + "\r\n" +
                     "Connection: close\r\n\r\n");
    // Ждём ответ от сервера
    while (client.connected()) {
      if (client.available()) {
        byte buffer[bufferSizeServ];
        int bytesRead = client.read(buffer, bufferSizeServ);
        file.write(buffer, bytesRead);
      }
    }
    Serial.println("File downloaded from: " + downloadUrl);
    file.close(); // Закрытие файла на SD-карте
    client.stop();
  } else {
    Serial.println("Failed to connect to server.");
    return;
  }
}
//===================== ========================================================================================= =====================//

//======================================= Функция установки первых значений =======================================//
/* 
Функция выполняется один раз при запуске устройства. Serial.begin() задает скорость передачи данных в битах в 
секунду (бод) для последовательной передачи данных. Далее инициализируем подключённые устройства и
вызываем первые функции для старта работы нашего устройства с начальными параметрами.
*/
void setup() {
  Serial.begin(115200);
  Wire.begin();
  initWiFi();
  delay(500);
  initDS3231();
  delay(500);
  initSD();
  delay(500);
  initOled();
  getJsonFromServ();
  parseJson();
  // устанавливаем таймер для запросов
  nextCheck = millis() + interval;
}
//===================== ===================================================================== =====================//

//======================================= Функция-петля или же повтор/цикл =======================================//
/*
Данная функция повторяется каждый раз при достижении конца написанного кода в ней.
В ней ...
*/
void loop() {
  if(onetime){
    if(initAud){
      initAudio((nameOfDir + "/" + filename + ".mp3").c_str());
      initAud = false;
    }
    if(played){
      playAudio();
      return;
    }
  }
  dateComparison();
  if (millis() > nextCheck) {
    nextCheck += interval;
    getJsonFromServ();
    parseJson();
  }
}
//===================== ==================================================================== =====================//

