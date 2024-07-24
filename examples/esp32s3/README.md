# ESP32-S3
Push H.264 video and OPUS audio to the WebRTC media server, such as Janus and ZLMediakit by WHIP

## Instructions

### Install esp-idf
at lease v5.2
```bash
$ git clone -b v5.2.2 https://github.com/espressif/esp-idf.git
$ cd esp-idf
$ source install.sh
$ source export.sh
```

### Download
```bash
$ git clone --recursive https://github.com/sepfy/libpeer
$ cd libpeer/examples/esp32s3
$ git clone --recursive https://github.com/sepfy/esp_ports.git components/srtp
```

### Configure
```bash
conda deactivate
cd /Users/bene/Dropbox/Dokumente/Promotion/PROJECTS/matchboxscope-webrtc/esp-idf
source export.sh
cd /Users/bene/Dropbox/Dokumente/Promotion/PROJECTS/matchboxscope-webrtc/examples/esp32s3
idf.py menuconfig
# Choose Example Connection Configuration and change the SSID and password
```

Modify the ```WHIP_HOST```, ```WHIP_PATH``` of ```src/config.h```

### Build 
```bash
cd /Users/bene/Dropbox/Dokumente/Promotion/PROJECTS/matchboxscope-webrtc/examples/esp32s3
idf.py build
```

### Test
```bash
cd /Users/bene/Dropbox/Dokumente/Promotion/PROJECTS/matchboxscope-webrtc/examples/esp32s3
idf.py flash
```


