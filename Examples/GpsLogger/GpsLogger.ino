/*
 * Author: B J lambert
 * 
 * Reads and decodes data from a BN-880 or similar GPS module
 * Writes a kml file with kml headers and footers and gps location and reletive altitude
 * the altitude is relative to the start location altitude
 * A txt file is saved with max distance(m) travelled and max relative altitude(m) gained.
 * The button stops logging and writes a kml footer to the file.
 * LED flashes when gps finds number of satellites >= MIN_SATS.
 * Connect BN-880 	TX to Arduino RX ***REMOVE WHEN PROGRAMMING THE ARDUINO***
 *		              RX to Arduino TX
 *                	GND to Arduino GND
 *               		VCC to Arduino 5V
 * decoder.begin(); and decoder.readRawData() must be run to collect GPS data
 * The baud rate is fixed at 9600
 * Note the sentences from the BN-880 start with GN and not GP as per the NMEA standard
 * To save variable memory the DATA_BUFFER_SIZE in the BN880Decoder.h file could be reduced in size from 250 bytes.
 * Reducing this may slow or stop the aquisition of the NMEA sentence.
 * With DATA_BUFFER_SIZE set to 250 bytes the aquistion of a GNGGA sentence is every 1-2 seconds.
 * Set SCANS_DIST_MEASURED distance is then the distance between two gps points, one at start of scan and one at end of scan
 * MIN_DISTANCE_MOVED set this in conjunction with above.
 * Example---
 * One gps scan is about 1 second. So moving at 3km/hour(walking pace)
 * Then distance moved in 10 gps scans = 8 metres.
 * If walking set SCANS_DIST_MEASURED = 10
 * and MIN_DISTANCE_MOVED to less than 8 say 4.
 * The lower set the more logs you get.
 * Too low and you are logging readings caused by small gps errors.
 * ---------
 * Logs GPS info to an SD card
 * SD card attached to SPI bus as follows:
 ** SDO - pin 12
 ** SDI - pin 11
 ** CLK - pin 13
 ** CS -  pin 10
*/

/*-----( Import needed libraries )-----*/

#include <BN880Decoder.h>
#include <SPI.h>
#include <SD.h>

/*-----( Declare Constants and Pin Numbers )-----*/
#define DEBUG 0 //for print statements on set DEBUG to 1
#if(DEBUG == 1)
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#define debugArg(x,y) Serial.print(x,y)
#define debuglnArg(x,y) Serial.println(x,y)
#else
#define debug(x)
#define debugln(x)
#define debugArg(x,y)
#define debuglnArg(x,y)
#endif

#define LED_PIN 9
#define MIN_SATS 5
#define MIN_DISTANCE_MOVED 4 //minimum distance(m) moved before a gps reading is logged
#define SWITCH_PIN 4
#define CHIPSELECT 10 //SD writer
#define SCANS_DIST_MEASURED 10//distance moved from first gps scan to last gps scan is calculated after this number of GPS scans.

/*-----( Declare objects )-----*/


BN880Decoder decoder;

/*-----( Declare Variables )-----*/
//const int CHIPSELECT = 10;//SD writer
bool refAltOk = false;//flag for reference alitude set
float refAlt = 0.0;//reference altitude
bool stopGPS = false;//flag for stop GPS
bool SDFileStarted = false;//flag for writing KML file headers
float totalDistance = 0.0;
float maxRelAlt = 0.0;
float minRelAlt = 0.0;

void setup()   /****** SETUP: RUNS ONCE ******/
{
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN,HIGH);
  pinMode(SWITCH_PIN, INPUT);
  decoder.begin();
  delay(1000);
  debugln("Initializing SD card...");
  if (!SD.begin(CHIPSELECT))
  {
    debugln(F("SD card failed"));
    while (true);
  }
  
}
//----end setup----

void loop()   /****** LOOP: RUNS CONSTANTLY ******/

{  
  decoder.readRawData();//read data from GPS module every 10 seconds //MUST INLUDE THIS LINE
  
 //**remove remarks to print the raw GNGGA sentence**
  //String result = (decoder.getSentence("GNGGA"));
    //if(result != "")
      //{
        //debugln(result);
      //} 
   
    if(decoder.getContentsGNGGA(SCANS_DIST_MEASURED))//distance apart measured every SCANS_DIST_MEASURED scans
      {
        float latSigned = 0.0;
        float lonSigned = 0.0;
        debug(F("Time "));
        debug(decoder.hours);
        debug(F(":"));
        debug(decoder.minutes);
        debug(F(":"));
        debug(decoder.seconds);
        debugln();
        debug(F("Latitude "));
        if(decoder.hemisphereNS == "N")
          {
            debugArg(decoder.latitude,6);
            latSigned = decoder.latitude;
          }
        else
          {
            debugArg(-decoder.latitude,6);
            latSigned = -decoder.latitude;
          }
        debugln(" " + decoder.hemisphereNS);
        debug(F("Longitude "));
        if(decoder.hemisphereEW == "E")
          {
            debugArg(decoder.longitude,7);
            lonSigned = decoder.longitude;
          }
        else
          {
            debugArg(-decoder.longitude,7);
            lonSigned = -decoder.longitude;
          }
        debugln(" " + decoder.hemisphereEW);
        debug(F("GPS fix "));
        if(decoder.gpsFix == "0")
          {
            debugln(F("Bad"));
          }
        else
          {
            debugln(F("Good"));
          }
        debug(F("Satellites "));
        debuglnArg(decoder.satellites,0);
        debug(F("Horizontal dilution of precision "));
        debuglnArg(decoder.hdop,2);
        debug(F("Altitude "));
        debugArg(decoder.altitude, 1);
        debugln(F(" metres"));
        debug(F("Height of geoid above WGS84 ellipsoid "));
        debugArg(-decoder.geoidHeight, 1);
        debugln(F(" metres"));
        debug(F("Distance between last two points ONLY displayed every "));
        debug(SCANS_DIST_MEASURED);
        debug(F(" GPS scans "));
        debugArg(decoder.distance, 1);
        debugln(F(" metres"));
        //sd card storage
        if(goodToGo() && !stopGPS)//sats found and not gps stopped then flash LED
          {
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));//flash led
          }
        if(decoder.distance > MIN_DISTANCE_MOVED && goodToGo() && refAltOk && !stopGPS)//moving and a good fix and reference altitude set
          {
                if(!SDFileStarted)//no KML file header so write one
                  {
                    writeHeader();
                    SDFileStarted = true;
                  }
                float relAlt = decoder.altitude - refAlt;//get relative altitude
                writeToSdCard(String(lonSigned, 7) + "," + String(latSigned, 6) + "," + String(relAlt));
                totalDistance += decoder.distance;
                if(relAlt > maxRelAlt)//save highest altitude
                  {
                    maxRelAlt = relAlt;
                  }
                if(relAlt < minRelAlt)//save lowest altitude
                  {
                    minRelAlt = relAlt;
                  }
          }
        else
          {
            if(goodToGo() && !refAltOk)//set refAlt
              {
                refAlt = decoder.altitude;//get first altitude as reference
                refAltOk = true;
              }
          }
        debugln(F("****************"));
      }
    if(digitalRead(SWITCH_PIN) == LOW && !stopGPS)//check if switch pressed to stop GPS
      {
        if(SDFileStarted)//only write footer if started
          {
        writeFooter();//Write footer to SD card and close file
          }
        stopGPS = true;
      }
      
}
//--Loop Ends---



//*********Functions************************

void writeToSdCard(String dataIn)
  {
    File dataFile = SD.open("datalog.kml", FILE_WRITE);
    if(dataFile)
      {
        dataFile.println(dataIn);
        dataFile.close();
        debugln(dataIn);
      }       
  }


void writeHeader()//write KML header to SD card and close file
  {
    File dataFile = SD.open("datalog.kml", FILE_WRITE);
    if(dataFile)
      {
        dataFile.println(F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
        dataFile.println(F("<kml xmlns=\"http://www.opengis.net/kml/2.2\">"));
        dataFile.println(F("<Document>"));
        dataFile.println(F("<Style id=\"yellowPoly\">"));
        dataFile.println(F("<LineStyle>"));
        dataFile.println(F("<color>7f00ffff</color>"));
        dataFile.println(F("<width>4</width>"));
        dataFile.println(F("</LineStyle>"));
        dataFile.println(F("<PolyStyle>"));
        dataFile.println(F("<color>7f00ff00</color>"));
        dataFile.println(F("</PolyStyle>"));
        dataFile.println(F("</Style>"));
        dataFile.println(F("<Placemark><styleUrl>#yellowPoly</styleUrl>"));
        dataFile.println(F("<LineString>"));
        dataFile.println(F("<extrude>1</extrude>"));
        dataFile.println(F("<tesselate>1</tesselate>"));
        dataFile.println(F("<!--altitudeMode>relativeToGround</altitudeMode-->"));
        dataFile.println(F("<coordinates>"));
        dataFile.close();
      }
  }

  
 void writeFooter()//write KML footer to SD card and close file
  {
    File dataFile = SD.open("datalog.kml", FILE_WRITE);
    if(dataFile)
      {
        dataFile.println(F("</coordinates>"));
        dataFile.println(F("</LineString></Placemark>"));
        dataFile.println(F("</Document></kml>"));
        dataFile.close(); 
      } 
    File distFile = SD.open("distlog.txt", FILE_WRITE);
    if(distFile)
      {
        distFile.print(F("Distance travelled(m) "));
        distFile.println(String(totalDistance));
        distFile.print(F("Max altitude(m) "));
        distFile.println(String(maxRelAlt));
        distFile.print(F("Min altitude(m) "));
        distFile.println(String(minRelAlt));
        distFile.close(); 
      } 
    }

    //check status of gps fix
    bool goodToGo()
      {
        bool fixGood = false;
        if(decoder.satellites >= MIN_SATS && decoder.gpsFix != "0")
          {
            fixGood = true;
          }
        return fixGood;
     }

 
 
 
