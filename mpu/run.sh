#!/bin/zsh

# Ativa o ambiente do ESP-IDF
source $HOME/esp/esp-idf/export.sh

# Define a porta serial e baud rate seguro
PORT="/dev/tty.usbserial-23210"
BAUD=115200

# Mensagem de status
echo "[ESP32] Compilando, fazendo flash e monitorando via $PORT @ ${BAUD}bps..."

# Compila o projeto
idf.py build

# Flash com baudrate seguro
idf.py -p $PORT -b $BAUD flash

# Orientação ao usuário
echo "[ESP32] Desconecte o IO0 de GND e pressione RESET, caso necessário."

# Monitor serial
idf.py -p $PORT -b $BAUD monitor
