Comandos:

1. source ~/esp/esp-idf/export.sh
2. idf.py set-target esp32 (so uma vez)
3. idf.py build
4. idf.py -p /dev/tty.usbserial-23210 -b 115200 flash monitor
5. idf.py fullclean (apagar diretorio build e cache)
