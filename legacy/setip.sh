#!/bin/bash

CONTROLLER=192.168.0.91

curl http://$CONTROLLER/eeprom/0/04
curl http://$CONTROLLER/eeprom/1/02
curl http://$CONTROLLER/eeprom/2/00
curl http://$CONTROLLER/eeprom/3/00
curl http://$CONTROLLER/eeprom/4/01
curl http://$CONTROLLER/eeprom/5/01
curl http://$CONTROLLER/eeprom/6/192
curl http://$CONTROLLER/eeprom/7/168
curl http://$CONTROLLER/eeprom/8/0
curl http://$CONTROLLER/eeprom/9/51
curl http://$CONTROLLER/reboot
