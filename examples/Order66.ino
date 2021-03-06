#include <Arduino.h>
#include <ESP8266WiFi.h>

#include <math.h>

#include "numbermap.h"
#include "uv_node.h"
#include "uv_transport.h"
#include "uv_transport_serial.h"
#include "uv_transport_tcp.h"
#include "uv_primitive.h"
#include "uv_app_heartbeat.h"
#include "uv_app_nodeinfo.h"
#include "uv_app_portinfo.h"
#include "uv_app_register.h"

// ADC Boot Configuration
// ADC_MODE(ADC_VCC); // configure the ESP8266 to internal voltage mode, otherwise defaults to external sense

char wifi_ssid[] = "ssid";    //  your network SSID (name)
char wifi_pass[] = "pass";   // your network password



// 64-bit microsecond timer lambda function, used for hardware-level events
// deals with the 70-minute wraparound, has an offset for aligning with external clocks
unsigned long last_micros = 0;
uint64_t system_micros_offset = 0;
auto system_time_us = []()->uint64_t {
  return system_micros_offset + (uint64_t)( micros() - last_micros );
};

// millisecond timer, used for most application-level loops
unsigned long last_time = 0;


HardwareSerialPort serial_port_0(&Serial);
DebugSerialPort debug_port_0(&serial_port_0,false);
UAVNode * uav_node;
TCPNode * tcp_node;
TCPNode * tcp_debug;

// OOB handlers
void oob(char *transport_name, SerialFrame* rx, uint8_t* buf, int count) {
  Serial.print(transport_name);
  Serial.print(": ");
  Serial.write(buf, count);
  Serial.println();
}

void serial_oob(UAVTransport *transport, SerialFrame* rx, uint8_t* buf, int count) {
  oob("ser", rx, buf, count);
}

void tcp_oob(UAVTransport *transport, SerialFrame* rx, uint8_t* buf, int count) {
  oob("tcp", rx, buf, count);
}

RegisterList* system_registers = new RegisterList();

void uavcan_setup() {
  // initialize uavcan node
  uav_node = new UAVNode();
  // attach the system microsecond timer
  uav_node->get_time_us = system_time_us;

  // convert address and mask into a LSB-first uint32, which for some reason is the opposite of the WiFi object's properties
  IPAddress ip = WiFi.localIP();
  IPAddress snet = WiFi.subnetMask();
  uint32_t addr = 0;
  for(int i=0; i<4; i++) addr = (addr<<8) | ( ip[i] & ~snet[i] );
  // set the node id to the last part of the subnet ip address
  uav_node->serial_node_id = addr & UV_SERIAL_NODEID_MASK;
  
  // add a loopback serial transport for testing
  SerialTransport* loopback = new SerialTransport( new LoopbackSerialPort(), true, serial_oob);
  // loopback->oob_handler = serial_oob;
  uav_node->serial_add( loopback );

  // enable the serial transport on debug port 0
  // SerialTransport* serial = new SerialTransport(&debug_port_0, false, serial_oob);
  // uav_node->serial_add( serial );

  // start a tcp server over the node on port 66
  tcp_node = new TCPNode(66,uav_node,false,tcp_oob);

  // start a debug tcp server over the node on port 67
  tcp_debug = new TCPNode(67,uav_node,true,tcp_oob);

  // start UAVCAN apps on the node, some of which will begin emitting messages
  HeartbeatApp::app_v1(uav_node);
  NodeinfoApp::app_v1(uav_node);
  RegisterApp::app_v1(uav_node, system_registers);

  // debug our port list
  uav_node->ports.debug_ports();

}

int jedi_purge = 1000;

void setup(){
  // initialize 64 bit microsecond timer offset
  last_micros = micros();
  system_micros_offset = last_micros;

  // initialize serial port
  Serial.begin(115200);
  // we don't want the debug output
  Serial.setDebugOutput(false);
  Serial.println();
  // start the filesystem
  // SPIFFS.begin();

  // connect to wifi
  WiFi.begin(wifi_ssid, wifi_pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("WiFi connected ");
  Serial.print(WiFi.localIP());
  Serial.print(" / ");
  Serial.print(WiFi.subnetMask());
  Serial.println();
  // start the network node
  uavcan_setup();
  Serial.print("UAVCAN Node ");
  Serial.print(uav_node->serial_node_id);
  Serial.println();

  // initialize millisecond timer
  last_time = millis();

  // begin main loop
}

void loop(){
  // update 64 bit system microsecond timer offset
  unsigned long current_micros = micros();
  system_micros_offset += (current_micros-last_micros);
  last_micros = current_micros;

  // milliseconds since last loop, deals with the 50 day cyclic overflow.
  unsigned long t = millis();
  int dt = t - last_time;
  last_time = t;

  // UAVCAN node tasks
  uav_node->loop(t,dt);
  // TCP server tasks
  tcp_node->loop(t,dt);
  tcp_debug->loop(t,dt);

  //
  if(true) {
    jedi_purge-=dt;
    if(jedi_purge<0) {
      jedi_purge = 9000;
      switch(random(4)) {
        case 0:
          NodeinfoApp::ExecuteCommand(uav_node, uav_node->serial_node_id, 66, "Execute order 66!", nullptr);
          break;
        case 1:
          NodeinfoApp::GetInfo(uav_node, 159, [](NodeGetInfoReply* r){
            if(r==nullptr) {
              Serial.println("no reply.");
            } else {
              Serial.println("got reply.");
            }
          }); // to windows machine, if available
          break;
        default:
          NodeinfoApp::GetInfo(uav_node, uav_node->serial_node_id, nullptr); // to self
          break;
      }
    }
  }

  // process OS tasks
  delay(100);
}

