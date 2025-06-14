#import "poster.typ": *

#show: poster.with(
  size: "36x24",
  title: "Implementierung eines Tracking Systems für Produkte auf einer Fertigungskette",
  authors: "Kilian Röper",
  departments: "Embedded Systems",
  univ_logo: "./images/DHBW_logo.jpg",
  footer_text: "NFC-Tracking System, 2025",
  footer_url: "https://www.ravensburg.dhbw.de/",
  footer_email_ids: "roeper.kilian-it22@it.dhbw-ravensburg.de",
  footer_color: "ebcfb2",

  // Modifying the defaults
  keywords: ("NFC", "MQTT", "RITZ"),
)

= Bandumlaufsystem & Tracking Technologien
\
Die Lernfabrik fertigt teilautomatisch Produkte über eine Fertigungskette, die als Bandumlaufsystem abgebildet ist (@bandumlauf). Die Produkte werden simulativ als Raspberry Pi in einer 3D-gedruckten Schachtel über das Band transportiert. Es gibt vier Stationen: Ein Werkerassistent zur Produktvorbereitung, ein Schraubroboter, zum Befestigen der Schachtel, ein Hochregal, in dem fertige Produkte eingelagert werden und eine End-of-Line-Station, an der Informationen vom Raspberry Pi abgenommen werden.\
Für die Steuerung und Nachverfolgbarkeit wurde ein System implementiert, das die Position und den Zustand der Produkte mittels NFC-Technologie registriert und diese an ein Manufacturing-Execution-System (MES) weitergibt. 
#figure(
  image("images/Bandumlaufsystem.png", width: 50%),
  caption: [Bandumlaufsystem im RITZ],
)<bandumlauf>
Um das zu ermöglichen wurde an jeweils einer der Stationen ein Node-MCU-Controller angebracht und mit einem NFC-Sensor verbunden. Diese kommunizieren per MQTT mit einem auf einem Raspberry Pi laufenden MQTT-Broker, der dann die Informationen an ein zentrales MES-System mit Node-Red-Visualisierung weiterleitet. Außerdem ist eine Schnittstelle zu einer SPS vorgesehen mit der das Band in Abhängigkeit vom Zustand der Tags gesteuert werden kann.
\
\
= MQTT & Systemarchitektur
\
MQTT (Message Queuing Telemetry Transport) ist ein leichtgewichtiges, offenes Netzwerkprotokoll für die asynchrone Kommunikation zwischen Geräten – besonders in IoT-Systemen (Internet of Things).

Es basiert auf dem Publisher/Subscriber-Modell:

+ Publisher senden Nachrichten zu einem Thema (Topic)
+ Subscriber abonnieren diese Themen und erhalten Nachrichten vom Broker

Die Node-MCU-Controller agieren sowohl als Publisher als auch als Subscriber, um Nachrichten sowohl zu senden als auch vor dem Beschreiben eines Tags zu empfangen(@architecture). 

#set align(center)
#figure(
  image("images/Systemarchitektur.png", 
        width: 100%),
  caption: [Systemarchitektur bestehend aus Raspberry Pi, Node-MCU, SPS und Manufacturing Execution System (MES)]
)<architecture>

#figure(
  table(
    columns:(auto, auto, auto), 
    inset:(10pt),
   [#strong[Variable]], [#strong[Typ]], [#strong[Erklärung]],
   [DEBUG], [define], [ermöglicht das Ein- und \ Ausschalten von Debug-Messages],
   [ssid], [char-pointer], [Netzwerk-SSID des \ Raspberry Pi],
   [password], [char-pointer], [Passwort zum Raspberry Pi],
   [topic_send], [char-array], [topic auf dem ein \ MCU-Controller sendet],
   [topic_receive], [char-array], [topic das ein MCU-Controller \ zum hören abonniert],
  ),
  caption: [Generische Softwarevariablen der Node-MCUs]
)<generics>

#set align(left)
Die Software auf den MCU-Controllern wurde einzeln entsprechend der Anforderungen einer Station implementiert und bietet generische Konfigurationsvariablen. @generics listet davon beispielhaft einige auf. \
Ein besonderes Merkmal der Systemarchitektur ist die Schnittstelle zu einer SPS von Siemens, mit das Bandumlaufsystem gesteuert werden kann (@architecture). Diese abonniert Topics vom MQTT-Broker, wodurch das Band bei empfangener Nachricht beispielsweise gestoppt oder gestartet werden kann. 
\
\

== NFC-Technologie
\
#box(
  fill: white,
  stroke: black,
  inset: 1em,
)[
  #heading[NFC – Near Field Communication]

  #grid(columns: 3, align: left, gutter: 2em)[
    #box(
      fill: luma(240),
      inset: 1em,
      stroke: black,
    )[
      #strong[Smartphone / NFC-Leser]  
      • Aktives Gerät  
      • Sendet + empfängt  
      • 13.56 MHz Trägerfrequenz  
      • Energiequelle
    ]
    #box(
      fill: luma(240),
      inset: 1em,
      stroke: black,
    )[
      #strong[NFC-Tag / Chip]  
      • Passives Gerät  
      • Empfängt Energie vom Leser  
      • Reflektiert Signal (Backscatter)  
      • Keine eigene Stromversorgung
    ]
  ]

  #text[Technische Merkmale:]
  • Reichweite: < 10 cm  
  • Frequenz: 13.56 MHz  
  • Kommunikation: Peer-to-Peer, Lesen/Schreiben und Kartenemulation  
  • Standard: ISO/IEC 14443 & 18092
]

\
= Sensor-Polling

#block(
  fill: luma(230),
  inset: 8pt,
  radius: 4pt,
  [
    Um Informationen erfolgreich auf einen NFC-Tag zu schreiben, muss sich der Tag für eine definierte Dauer im Erfassungsbereich des NFC-Sensors befinden und zuverlässig erkannt werden. Dazu wird ein Polling-Verfahren eingesetzt: Der Sensor überprüft kontinuierlich, ob ein Tag in Reichweite ist. Solange der Tag erkannt wird, ist ein Schreibvorgang möglich. Überschreitet die Erkennungsdauer eine durch die Variable INSCOPE_TIME festgelegte Schwelle, wird der Vorgang als erfolgreich gewertet. In diesem Fall blockiert der Controller kurzzeitig die weitere Verarbeitung, um die Nachricht sicher an den MQTT-Broker zu übermitteln.
  ]
)
Das folgende Codebeispiel verdeutlicht das Prinzip des Pollings:
Der NFC-Sensor wird in einer Schleife kontinuierlich abgefragt, um zu prüfen, ob sich ein Tag im Erfassungsbereich befindet. 


```c
void loop(void) {
  if (read_NFC_sensor()) {
    Serial.println("Tag gefunden");
  } else {
    Serial.println("auf einen Tag warten");
  }
  // Kurze Pause (1ms)
  delay(1);
}
```
\

= Visualisierung in Node-Red

Node-RED ist eine webbasierte, visuelle Entwicklungsumgebung für die verdrahtungsfreie Programmierung von Datenflüssen. Dieses Tool wurde verwendet, um ein graphisches Live-Tracking der Produkte auf dem Band zu ermöglichen.

- Anforderungen an das Dashboard (@nodered):
  - Visuelle Abbildung der Stationen und des Bandumlaufsystems
  - Aktualisierung der Positionen der Produkte entlang des Bandes 
  - tabellarische Auflistung der Produkte
- Gehalt der Tabelle:
  - UID: Identifikationsnummer eines Tags
  - tracking: Position auf dem Band
  - state: Zustand an der letzten Station
  - finished: im Hochregal eingelagert?

#block(
  fill: luma(230),
  inset: 8pt,
  radius: 4pt,
  [
    Das erstellte Dashboard bietet außerdem die Möglichkeit testweise Nachrichten an die MCU-Controller zu senden. Das wird über die integrierten MQTT-Blöcke in Node-Red ermöglicht.
  ]
)

#figure(
  image("images/node-red-band.jpeg", 
        width: 85%),
  caption: [Visualisierung des Bandumlaufsystems in Node-Red]
)<nodered>