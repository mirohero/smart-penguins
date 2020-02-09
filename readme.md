# Smart Penguins
A prototype for meshbased car2x system by A. Köcher, K. Keskinsoy,S. Schmidt and M.Bilge.
The system was implemented on the head of fruitymesh a battery efficient, Bluetooth Low Energy mesh network. 

## Features
- Detection of traffic jams, accidents, black ice and emergency cars 
- Passing these informations to previous road user
- Determination if the incident is in the direction of travel or irrelevant for the node

## Use Cases

![](SmartPenguin.gif)
[Full video](SmartPenguin.mp4)





## Smart Penguins mit Docker

- [1. Aufsetzen der Softwareumgebung durch Docker](#sw_env) 
    * [1.1 Docker installieren (MacOS)](#inst_docker)
    * [1.2 Freigeben des seriellen Ports vom Hosts zum Docker-Container](#serial_port)
    * [1.3 Bauen des Docker-Images](#build_docker)
- [2. Ausführen des Docker-Containers](#cont_docker)
- [3. Bauen und Flashen](#build)
- [4. Debugging durch JLink](#debugging)
<!-- toc -->

<a name="sw_env"></a>
## 1. Aufsetzen der Softwareumgebung durch Docker

Um Anwendungen für das nRF52 Development Kit zu entwickeln, wird folgendes benötigt:

- PC, der mit einem Windows-, Linux- oder Mac-Betriebssystem ausgestattet ist.
- GNU ARM Embedded Toolchain
- nRF5 Connect SDK
- nRF5 Command Line Tools
- SEGGER J-Link Software
- Das nRF52 Development Kit selbst und ein USB-Kabel zum Anschluss an den PC.

Folge den Anweisungen der folgenden Kapitel, um das Docker-Image ```docker-nrf5``` zu bauen und davon einen Container zu instanziieren. 

<a name="inst_docker"></a>
#### 1.1 Docker installieren (MacOS)

1. Brew installieren: https://brew.sh
2. Docker Engine und Docker-Machine installieren:<br/>```$ brew install docker```
3. Docker Client installieren:<br/>```$ brew cask install docker```
4. Optional: Falls Virtualbox nach dem 2. Schritt fehlt. Virtualbox installieren mit:<br/>```$ brew cask install virtualbox```

<a name="serial_port"></a>
#### 1.2 Freigeben des seriellen Ports vom Hosts zum Docker-Container (MacOS)

Zunächst muss die "SEGGER J-Link" Software installiert werden.

Lade die Software herunter und folge den Anweisungen auf der Website (https://www.segger.com/downloads/jlink).

Die nächsten Schritte zeigen, wie ein serieller Port vom Host an den Docker-Container freigegeben werden kann.
Hierfür muss zunächst ein Virtualbox Treiber für die Docker-Maschine erzeugt werden.

1. ```$ docker-machine create --driver virtualbox default```
2. Überprüfe ob der Treiber erzeugt wurde: ```$ docker-machine ls```
3. Stoppe die Docker-Maschine: ```$ docker-machine stop```
4. Verbinde das nRF52 Development Kit mittels USB.
5. Jetzt kann die VM konfiguriert und der serielle Port exportiert werden. Öffne die Virtualbox Anwendung. Wähle die 'default' VM aus und klicke auf den 'Settings' Button, um die USB Einstellungen vorzunehmen.
<img src="images/virtualbox_default_settings.png" width="500" alt="Virtualbox 'default' VM Einstellungen">

6. Klicke auf 'Ports' und anschließend auf den 'USB' Tab. Aktiviere die 'Enable USB Controller' Checkbox. Wähle 'USB 2.0 (EHCI) Controller' aus. Füge einen USB Filter hinzu (USB Icon mit grünem Plus Symbol). Wähle den 'SEGGER J-Link [0100]' Treiber aus.
<img src="images/virtualbox_port_usb.png" width="500" alt="Virtualbox serieller Port">

7. Falls USB 2.0 nicht ausgewählt werden kann, muss der 'Oracle VM VirtualBox Extension Pack' installiert werden. Die Anweisungen für die Installation befindet sich hier: https://www.virtualbox.org/wiki/Downloads
8. Entferne das USB Kabel vom nRF52 Development Kit
9. Starte die Docker-Maschine mit: ```$ docker-machine start```
10. Es müssen einige Umgebungsvariablen gesetzt werden, damit Docker die VM verwendet anstelle des nativen Modus. Das ```$ docker-machine env``` Kommando gibt die notwendigen Schritte für das Setzen der Variablen an. Führe folgenden Befehl aus um diese zu setzen: ```$ eval "$(docker-machine env default)"```

Der serielle Port des Hosts ist nun vom Docker-Container aus ansprechbar.

<a name="build_docker"></a>
#### 1.3 Bauen des Docker-Images

Die Kapitel [1.4 Bauen des Docker-Images](#build_docker) und [2. Ausführen des Docker-Containers](#cont_docker) können übersprungen werden, wenn diese beiden Schritte zeitgleich ausgeführt werden sollen. Wechsle hierzu in das ```docker``` Verzeichnis und führe das folgende Script aus:

1. ```$ cd <project_path>/docker```
2. ```$ ./docker.sh```

Führe folgende Befehle aus, wenn nur das Docker-Image gebaut werden soll:

Das Docker-Image muss nur einmal gebaut werden. Mit ```$ docker images``` kann überprüft werden, ob das Image bereits installiert wurde.

Das Image muss nicht installiert werden, falls die Zeile ```docker-nrf5``` im Terminal ausgegeben wird.

Falls ```docker-nrf5``` im Terminal nicht erscheint, müssen die folgenden Schritte ausgeführt werden:

1. Wechsle das Verzeichnis indem sich die ```Dockerfile``` befindet:<br/>```$ cd <project_path>/docker```
2. Baue das Docker-Image: ```$ docker build -t docker-nrf5 .```

Im 2. Schritt wird ein Ubuntu-Image gebaut und die vorausgesetzten Packages für die Toolchain installiert. Der Befehl führt nach der Ausführung einen Script aus, der die Toolchain installiert.

<a name="cont_docker"></a>
## 2. Ausführen des Docker-Containers

Stelle sicher, dass Docker installiert, und der serielle Port des Hosts an den Docker-Container freigegeben wurde, bevor der Docker container gestartet werden soll.

Führe folgende Befehle aus, um einen Container vom ```docker-nrf5``` Image zu instanziieren:

1. Wechsle in den ```docker``` Ordner im Projektverzeichnis: ```$ cd <project_path>/docker```
2. Starte den Container mit dem Script: ```$ ./docker.sh```. Dieser Script startet einen Container für das ```docker-nrf5``` Image. Der Script mountet zudem das Projektverzeichnis in das ```/smart-pinguins``` Verzeichnis des Containers. Zudem exportiert er den Hosts ```/dev/ttyUSB0``` Port an den Port ```/dev/ttyUSB0``` des Containers.

Kann der Serielle Port nicht an den Container freigegeben werden, dann führe folgende Befehle im Kapitel [1.2 Freigeben des seriellen Ports vom Hosts zum Docker-Container (MacOS)](#serial_port) aus.

<a name="build"></a>
## 3. Bauen und Flashen

1. Baue das Projekt mit ```$ make ENV=docker``` aus dem Projektverzeichnis ```smart-pinguins/fruitymesh```.
2. Der Hex-Output ```FruityMesh.hex``` befindet sich im folgenden Pfad: ```smart-pinguins/fruitymesh/_build/release/NRF52/github/```.
3. Um das Development Kit zu Flashen führe folgenden Befehl im Pfad ```smart-pinguins/fruitymesh/_build/release/NRF52/github/``` aus:
```nrfjprog --program FruityMesh.hex  --sectorerase -r```

<a name="debugging"></a>
## 4. Debugging durch JLink

1. Führe folgenden Befehl aus, nachdem der ```docker.sh``` Script ausgeführt wurde:<br/>```$ JLinkRTTClient```
2. Öffne eine neue zusätzliche Verbindung zum ```nrf5``` Docker Container (im neuen Terminal Fenster):<br/>```$ docker exec -ti nrf5 /bin/bash```
3. Führe folgenden Befehl aus:<br/>```$ JLinkExe  -AutoConnect 1 -device NRF52832_XXAA -speed 4000 -if SWD```

Nun werden die Logs im ersten Terminal Fenster ausgegeben.
