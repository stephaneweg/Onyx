# Ledger — Gebruikershandleiding

*Dubbel boekhouden voor Belgische vennootschappen en zelfstandigen, op Onyx*

Uitgave september 2026, voor Ledger zoals het met Onyx wordt geleverd. De afbeeldingen tonen de
demovennootschap die bij Ledger hoort, *Atelier Lumen SRL*, een kleine designstudio in Brussel.

## Inhoud

- [1. Inleiding](#1-inleiding)
- [2. Aan de slag](#2-aan-de-slag)
- [3. Het venster](#3-het-venster)
- [4. De boekhouding instellen](#4-de-boekhouding-instellen)
- [5. Klanten en leveranciers](#5-klanten-en-leveranciers)
- [6. Verkopen](#6-verkopen)
- [7. Aankopen](#7-aankopen)
- [8. Leveranciers betalen (SEPA)](#8-leveranciers-betalen-sepa)
- [9. Bank en kas](#9-bank-en-kas)
- [10. Diverse verrichtingen](#10-diverse-verrichtingen)
- [11. Offertes, bestellingen en leveringsbonnen](#11-offertes-bestellingen-en-leveringsbonnen)
- [12. Documenten afdrukken vanuit sjablonen](#12-documenten-afdrukken-vanuit-sjablonen)
- [13. Rapporten](#13-rapporten)
- [14. Btw](#14-btw)
- [15. Het boekjaar afsluiten](#15-het-boekjaar-afsluiten)
- [16. Bestanden en back-ups](#16-bestanden-en-back-ups)
- [17. Vragen en antwoorden](#17-vragen-en-antwoorden)
- [18. Woordenlijst](#18-woordenlijst)
- [Bijlage A. De btw-codes](#bijlage-a-de-btw-codes)
- [Bijlage B. De samenvoegvelden](#bijlage-b-de-samenvoegvelden)
- [Bijlage C. Het toetsenbord](#bijlage-c-het-toetsenbord)

## 1. Inleiding

### 1.1 Wat Ledger doet

Ledger voert de volledige dubbele boekhouding van een Belgische vennootschap — een BV, NV, CV, vzw — of
van een zelfstandige. Het volgt het Belgische rekeningenstelsel, het **MAR** (*Minimum Algemeen
Rekeningenstelsel*; in het Frans het *PCMN*), en de Belgische btw-regels, en het maakt de bestanden
waarmee de Belgische administratie en de banken werken:

- **facturen** en **creditnota's** voor verkopen en aankopen, met alle Belgische btw-gevallen: de
  tarieven van 6, 12 en 21 %, intracommunautaire leveringen en diensten, uitvoer, de verlegging van
  heffing naar de medecontractant, niet-aftrekbare btw, de 50 % van de bedrijfswagen;
- **offertes**, **bestellingen**, **leveringsbonnen** en **bestelbonnen** voor leveranciers, telkens
  omgezet in het volgende document en ten slotte in de factuur;
- documenten **afgedrukt vanuit sjablonen** door Letters, in het Nederlands, Frans of Engels, met uw
  briefhoofd;
- **bank- en kasuittreksels**, ingevoerd of **geïmporteerd uit de CODA-bestanden van uw bank**, waarbij
  de facturen die ze betalen voor u worden gevonden en afgepunt;
- de **betaling van uw leveranciers** via een **SEPA**-overschrijvingsbestand voor uw bank;
- de periodieke **btw-aangifte** als **Intervat**-XML-bestand, de **jaarlijkse klantenlisting** en de
  **intracommunautaire opgave**;
- **rapporten**: dagboeken, grootboek, proef- en saldibalans, balans, resultatenrekening, saldi van
  klanten en leveranciers, vorderingen en schulden volgens ouderdom — op het scherm, als
  Letters-documenten of als rekenbladen;
- de **jaarafsluiting**, met overdracht van het resultaat.

U voert **documenten** in — een factuur, een rekeninguittreksel —, nooit losse boekingen: Ledger maakt
de boeking, toont ze tijdens het typen en boekt ze bij het opslaan. Alles wordt meteen in het bestand
van de vennootschap geschreven: er is geen aparte stap "opslaan" voor de boekhouding.

### 1.2 Voor wie

Ledger is bedoeld voor wie zijn eigen boekhouding voert of die van een kleine onderneming: een
freelance grafisch ontwerper, een consultant, een kleine winkel, een kleine vennootschap met enkele
honderden facturen per jaar. De functies volgen die van **BOB 50**, het Belgische
boekhoudprogramma, met de eenvoud van **GnuCash**. U hoeft geen boekhouder te zijn om het te gebruiken,
maar u moet de basis van het dubbel boekhouden kennen: paragraaf 1.5 herhaalt die.

> **Belangrijk.** Ledger helpt u een correcte boekhouding te voeren en uw btw-bestanden voor te
> bereiden. Het vervangt het advies van uw accountant-boekhouder niet: laat uw jaarcijfers nakijken door
> een professional voor u uw jaarrekening neerlegt.

### 1.3 Wat u nodig hebt

- Onyx, op een Raspberry Pi 4 (of de Onyx-desktopsimulator op een pc).
- **Letters**, met Onyx geïnstalleerd: het drukt uw offertes, facturen en rapporten af.
- Het **rekenblad** (**Spreadsheet**, facultatief): het opent rapporten als werkmappen.
- Om uittreksels te importeren: de **CODA**-bestanden van uw bank (uit uw online bankieren, meestal
  `.cod`- of `.txt`-bestanden), gekopieerd naar de SD-kaart.
- Om btw-aangiften in te dienen: toegang tot **Intervat**, de site van de FOD Financiën, waar u de
  XML-bestanden oplaadt die Ledger schrijft.

### 1.4 Over deze handleiding

- De schermen van Ledger zijn in het **Engels**. Knoppen, menu's en velden staan daarom in **vet**,
  zoals op het scherm, zo nodig gevolgd door hun vertaling: **Save** (opslaan), **New invoice**
  (nieuwe factuur).
- Een menuopdracht wordt geschreven als **File ▸ Open...**: het menu **File**, dan het item **Open...**.
- Toetsen worden geschreven als `Ctrl+N`, `Tab`, `Enter`, `Esc`, `F4`.
- Bedragen worden op zijn Belgisch geschreven: `1.234,56` — een punt groepeert de duizendtallen, een
  komma scheidt de centen. Datums worden geschreven als `28/09/2026`.
- De voorbeelden gebruiken de **demovennootschap**, *Atelier Lumen SRL* (paragraaf 2.2). U kunt er alles
  op uitproberen: het is een apart bestand, los van uw boekhouding.

### 1.5 Boekhouden in een notendop

- **Rekeningen.** Elk bedrag wordt geboekt op de rekeningen van het rekeningenstelsel. Het MAR nummert
  ze per klasse: **1** eigen vermogen en schulden op lange termijn, **2** vaste activa, **3** voorraden,
  **4** vorderingen en schulden (400000 klanten, 440000 leveranciers, 451000 te betalen btw, 411000
  terug te vorderen btw...), **5** bank en kas (550000, 570000), **6** kosten, **7** opbrengsten. De
  klassen 1 tot 5 vormen de **balans**, de klassen 6 en 7 de **resultatenrekening**.
- **Dubbel boekhouden.** Elke verrichting is een **boeking** waarvan het **debet** gelijk is aan het
  **credit**. Een verkoopfactuur van 1.000,00 plus 210,00 btw debiteert de klant voor 1.210,00 (hij is
  u dat verschuldigd) en crediteert de verkopen voor 1.000,00 (een opbrengst) en de te betalen btw voor
  210,00 (u bent die de Staat verschuldigd). Ledger schrijft die lijnen voor u.
- **Dagboeken.** Boekingen worden bewaard in **dagboeken** volgens hun aard — verkopen, aankopen, bank,
  kas, diverse verrichtingen —, die elk hun documenten vanaf 1 nummeren per boekjaar.
- **Debet- en creditsaldi.** Een rekening heeft een **debetsaldo** (D) als het debet groter is dan het
  credit — een actief, een kost, een klant die u iets verschuldigd is — en een **creditsaldo** (C) in
  het andere geval — eigen vermogen, een schuld, een opbrengst.
- **Afpunten.** Een factuur en de betaling die ze vereffent worden **afgepunt**: samen maken ze nul op de
  rekening van de derde, en de factuur staat als betaald (**Paid**). Ledger punt ze af wanneer u een
  betaling tegen haar factuur boekt.
- **Boekjaar.** De periode waarvoor de rekeningen worden afgesloten, meestal het kalenderjaar. Aan het
  einde ervan wordt het **resultaat** — de opbrengsten min de kosten — naar het eigen vermogen
  overgedragen.

## 2. Aan de slag

### 2.1 Ledger starten

Start **Ledger** vanuit de lijst van toepassingen (groep **Productivity**) of vanuit het dock. Ledger
opent ook met een dubbelklik op een `.ledger`-bestand in de File Viewer, of wanneer u er een op zijn
venster sleept.

Bij het starten opent Ledger de boekhouding die de vorige keer open was. De allereerste keer toont het
zijn welkomstpagina.

![De welkomstpagina](images/welcome.png)
*De welkomstpagina: uw vennootschap aanmaken, een bestaande boekhouding openen of de demovennootschap uitproberen.*

### 2.2 De demovennootschap uitproberen

Klik op **Try the demo company** om `SD:/docs/demo-company.ledger` te openen: *Atelier Lumen SRL*, een
designstudio met 23 klanten en leveranciers en een 330-tal documenten van januari 2025 tot september
2026 — facturen, creditnota's, bankuittreksels, ingediende btw-aangiften, offertes en bestellingen.
Boekjaar 2025 is afgesloten; 2026 loopt. De demo is de beste manier om Ledger te leren kennen: alle
afbeeldingen in deze handleiding komen eruit.

Bij de demo hoort ook een bankbestand, `SD:/docs/demo-bank-statement.cod`, om de CODA-import uit te
proberen (paragraaf 9.3).

### 2.3 Uw vennootschap aanmaken

Klik op **Create a company...** op de welkomstpagina, of kies **File ▸ New Company...**.

![Het venster New company](images/new-company.png)
*Een nieuwe vennootschap: naam, btw-nummer, adres, bankrekening, rekeningenstelsel, btw-stelsel en eerste boekjaar.*

1. Typ de **naam** van de vennootschap met haar rechtsvorm (*Studio Nova SRL*), haar **btw-nummer**
   (`BE 0456.789.034`: Ledger controleert het), haar **straat**, **postcode** en **gemeente**, haar
   **e-mail**, **telefoon** en **bankrekening** (IBAN).
2. Kies het **rekeningenstelsel** (**Chart of accounts**): **Français (PCMN)** of **Nederlands (MAR)**.
   De taal van het rekeningenstelsel is ook die van uw documenten, tenzij de fiche van een klant anders
   bepaalt.
3. Kies het **btw-stelsel** (**VAT**):
   - **Files VAT returns** (dient btw-aangiften in), **Quarterly** (per kwartaal) of **Monthly** (per
     maand): het gewone geval;
   - **Small business franchise**: de vrijstellingsregeling voor kleine ondernemingen — u rekent geen btw
     aan en dient geen periodieke aangifte in;
   - **Not subject to VAT**: voor een activiteit die vrijgesteld is door artikel 44 van het
     Btw-wetboek (een arts, een opleidingscentrum...).
4. Typ het **eerste boekjaar** (**First fiscal year**), de eerste en de laatste dag (`01/01/2026` tot
   `31/12/2026`). Een boekjaar kan tot 24 maanden duren (het eerste van een vennootschap duurt vaak
   langer dan een jaar).
5. Klik op **Create** en kies waar het bestand wordt opgeslagen: `SD:/docs/Studio Nova SRL.ledger` wordt
   voorgesteld.

Ledger maakt de boekhouding aan: het volledige rekeningenstelsel in de gekozen taal (bijna 500
rekeningen), de dagboeken — **VKP** verkopen, **AKP** aankopen, **BNK** bank, **KAS** kas, **DIV**
diverse verrichtingen (in het Frans: **VEN**, **ACH**, **BNK**, **CAI**, **OD**) — en het eerste
boekjaar. Alles kan achteraf worden gewijzigd in **Settings** (hoofdstuk 4).

> **Tip.** Ga daarna naar **Settings ▸ Company** om uw rechtsvorm, uw register (*RPR Gent*), uw BIC en
> uw website toe te voegen: ze komen op uw afgedrukte documenten. En naar **Settings ▸ Journals** om het
> IBAN van uw bankrekening in te vullen in het dagboek **BNK**: de CODA-import en de betaling van
> leveranciers hebben het nodig.

### 2.4 Een boekhouding openen, meerdere vennootschappen

Elke vennootschap heeft haar eigen bestand. **File ▸ Open...** (`Ctrl+O`) opent de boekhouding van een
andere vennootschap; de geopende wordt gesloten (ze hoeft niet te worden opgeslagen). Voer zoveel
vennootschappen als u wilt, elk in een eigen bestand.

**File ▸ Save a Copy As...** schrijft een kopie van de boekhouding onder een andere naam — voor een
experiment, of om uw boekhouder een kopie te geven.

De naam van het bestand staat onderaan de zijbalk, met een groene stip. De stip wordt rood als het
bestand niet kon worden geschreven (kaart vol of tegen schrijven beveiligd): de boekhouding blijft dan
in het geheugen, en Ledger meldt het — sla er meteen elders een kopie van op.

## 3. Het venster

### 3.1 De onderdelen

![De onderdelen van het venster van Ledger](images/window-parts.png)
*De pagina Overview (overzicht) van de demovennootschap, en de onderdelen van het venster.*

1. **De geopende vennootschap**: haar naam en btw-nummer.
2. **Het getoonde boekjaar.** De lijsten, de rapporten en de btw-pagina tonen dit boekjaar; kies hier
   een ander. Nieuwe documenten worden erin gedateerd.
3. **De pagina's**, in groepen: **Overview** (overzicht); de dagboeken — **Sales** (verkopen),
   **Purchases** (aankopen), **Bank and cash** (bank en kas), **Misc. operations** (diverse
   verrichtingen); **Quotes and orders** (offertes en bestellingen); de derden — **Customers** (klanten),
   **Suppliers** (leveranciers); de boekhouding — **Chart of accounts** (rekeningenstelsel), **Reports**
   (rapporten), **VAT** (btw); en **Settings** (instellingen). Een rode **badge** telt wat op u wacht:
   achterstallige facturen bij **Sales** (oranje bij **Purchases**: uw eigen laattijdige betalingen), een
   laattijdige btw-aangifte bij **VAT**.
4. **Het bestand** van de boekhouding en zijn toestand (groen: geschreven).
5. **De titel van de pagina**, en een regel erover (hier het boekjaar).
6. **De knoppen van de pagina.** De gekleurde is haar hoofdactie (hier: een nieuwe verkoopfactuur).
7. **De tegels van het overzicht**: wat uw klanten u verschuldigd zijn (en hoeveel daarvan
   achterstallig is), wat u uw leveranciers verschuldigd bent, bank en kas, het resultaat en de omzet van
   het boekjaar.
8. **De verkopen en aankopen** van het boekjaar, maand per maand (zonder btw).
9. **De volgende btw-aangifte** (periode, vervaldag en bedrag tot nu toe) en **de meest achterstallige
   facturen** — klik er een aan om ze te openen.

### 3.2 Lijsten

De meeste pagina's zijn lijsten: de verkopen, de uittreksels, de klanten...

- **Filters** linksboven: **All** (alle), **Open**, **Overdue** (achterstallig), **Paid** (betaald) voor
  facturen.
- Het vak **Search** rechtsboven vindt woorden waar ook in een rij: een naam, een omschrijving, een
  nummer, een bedrag (`1.657,70` of `1657.70`). Alle getypte woorden moeten worden gevonden. `Ctrl+F`
  gaat erheen.
- Klik op de **titel van een kolom** om erop te sorteren; klik opnieuw om de volgorde om te keren.
- **Dubbelklik** op een rij, of druk op `Enter`, om ze te openen. `Delete` verwijdert de gekozen rij
  (als dat mag, en na bevestiging).
- Klik met de **rechtermuisknop** op een rij voor haar andere opdrachten.
- De **voet** van de lijst telt de getoonde rijen op.

### 3.3 Documenten

Een document — een factuur, een uittreksel, een verrichting, een offerte — opent **in de plaats van zijn
lijst**. Zijn velden staan bovenaan, zijn **lijnen** in een raster eronder, zijn totalen onderaan.

- **Save** (`Ctrl+S`) boekt het document en keert terug naar de lijst; **Save & New** boekt het en begint
  het volgende; **Cancel** (`Esc`) verlaat het — Ledger vraagt het eerst als er iets werd getypt; de rode
  **prullenbak** verwijdert het.
- Een veld voor een **derde**, een **rekening** of een **btw-code** zoekt terwijl u typt: een naam, een
  code, een btw-nummer, het nummer van een rekening of woorden uit haar naam. De lijst met overeenkomsten
  opent eronder; `Enter` of `Tab` neemt de opgelichte, `F4` of `Alt+↓` toont ze allemaal.
- **Datums** kunnen worden getypt als `28/09/2026`, `28.9.26`, `28/9` (dit jaar) of `280926`; de knop
  rechts van een datumveld opent een kalender.
- **Bedragen** kunnen worden getypt als `1850`, `1850,00`, `1.850,00` of `1850.00`.

![Een klant vinden tijdens het typen](images/party-picker.png)
*Een derde gevonden tijdens het typen: "Brou" vindt Brouwerij De Klok NV (haar code, haar gemeente).*

In het **raster met lijnen** typt u zoals in een rekenblad:

| Toets | Wat ze doet |
|---|---|
| `Tab`, `Enter` | Neemt wat getypt werd en gaat naar de volgende cel; na de laatste een nieuwe lijn |
| `Shift+Tab` | De vorige cel |
| `↑`, `↓` | De lijn erboven, eronder |
| `Esc` | Zet terug wat de cel bevatte (nog eens: verlaat het raster) |
| `F4`, `Alt+↓` | De lijst van wat in de cel kan worden getypt |
| `Ctrl+Delete` | Verwijdert de lijn (het kruisje aan het einde ervan ook) |

Een cel die niet kan aannemen wat u typte, krijgt een rode rand, en de reden verschijnt onderaan het
venster.

### 3.4 Vergrendelde documenten

Een document kan niet meer worden gewijzigd of verwijderd:

- als zijn **boekjaar is afgesloten** (hoofdstuk 15), of
- als het btw bevat en de **btw-aangifte van zijn periode als ingediend is gemarkeerd** (paragraaf 14.3).

Zijn pagina meldt dan **Locked** (vergrendeld) en waarom. Om het te verbeteren heropent u de periode of
het boekjaar — of, beter, u boekt een verbeterend document (een creditnota, een diverse verrichting) in
de lopende periode.

Verkoopfacturen worden zonder onderbreking genummerd, zoals de wet het vraagt: alleen het **laatste**
verkoopdocument van een dagboek kan worden verwijderd. Om een ander te annuleren, maakt u er een
**creditnota** voor (paragraaf 6.5).

## 4. De boekhouding instellen

### 4.1 De vennootschap

**Settings ▸ Company** bevat wat uw documenten als briefhoofd afdrukken en wat uw btw-bestanden
aangeven.

![Settings, de vennootschap](images/settings-company.png)
*De gegevens van de vennootschap: naam, rechtsvorm, adres, btw-nummer, contact, bank, register, website, btw-stelsel.*

- **Name** en **Legal form**: de naam en de rechtsvorm (*BV*, *NV*, *CV*, *vzw*...).
- **Street**, **City** (postcode en gemeente), **Country** (`BE`).
- **VAT number**: gecontroleerd tijdens het typen (**Valid**, **Wrong check digits** — foute
  controlecijfers —, **Not a VAT number**).
- **E-mail**, **Phone**, **Web site**.
- **IBAN** en **BIC**: afgedrukt op uw facturen.
- **Register**: het rechtspersonenregister en zijn rechtbank, *RPR Gent*, *RPR Brussel*.
- **VAT situation** en **Returns**: zoals bij het aanmaken (paragraaf 2.3); wijzig ze als uw situatie
  verandert.

Klik op **Save the changes**.

### 4.2 Boekjaren

![Settings, de boekjaren](images/settings-years.png)
*De boekjaren: hun datums, hun toestand en hun resultaat.*

De lijst toont elk boekjaar, zijn datums, zijn toestand (**Open** of **Closed**, afgesloten) en zijn
**resultaat** (winst of verlies tot nu toe).

- **Add the next year** voegt een boekjaar toe na het laatste, even lang.
- **Close the year...** en **Reopen the year**: zie hoofdstuk 15.

De balansrekeningen lopen vanzelf door van boekjaar tot boekjaar: u typt nooit een openingsboeking tussen
twee boekjaren van dezelfde boekhouding.

### 4.3 Dagboeken

![Settings, de dagboeken](images/settings-journals.png)
*De dagboeken: hun code, naam, soort, rekening en IBAN.*

Een vennootschap heeft een dagboek van elke soort nodig; voeg er andere toe om documenten te scheiden —
een tweede bankrekening, een tweede verkoopreeks (**VKP2** voor een winkel).

![Een dagboek](images/journal-dialog.png)
*Een bankdagboek: code, naam, soort, rekening (55xxxx) en IBAN.*

Dubbelklik op een dagboek (of kies het en klik op **Edit...**), of klik op **New journal...**:

- **Code**: 1 tot 5 letters of cijfers, afgedrukt in de documentnummers (*VKP 2026/0054*).
- **Name** en **Kind**: de naam en de soort — verkopen, aankopen, bank, kas of diversen (de soort van
  een dagboek kan niet meer veranderen zodra het documenten bevat).
- **Account**: de rekening van een bankdagboek (550000, 550100 voor een tweede bank...), van een
  kasdagboek (570000).
- **IBAN**: het nummer van de bankrekening. **De CODA-import vindt het dagboek van een uittreksel aan de
  hand ervan**, en de betalingen aan leveranciers vertrekken van deze rekening.
- **Hidden**: een verborgen dagboek, dat niet meer wordt voorgesteld voor nieuwe documenten (zijn
  documenten blijven).

### 4.4 De rekeningen per rol

![Settings, de rekeningen per rol](images/settings-accounts.png)
*De rekeningen die Ledger voor zijn eigen boekingen gebruikt.*

Sommige lijnen boekt Ledger zelf — de klant- en leverancierszijde van een factuur, de btw, het resultaat
van het boekjaar. **Settings ▸ Accounts** zegt op welke rekeningen: **Customers** (400000),
**Suppliers** (440000), **VAT due** (te betalen btw, 451000), **VAT deductible** (terug te vorderen
btw, 411000), **Profit carried forward** (overgedragen winst, 140000), **Loss carried forward**
(overgedragen verlies, 141000) en de **Suspense account** (wachtrekening, 499000). Behoud de rekeningen
van het MAR tenzij uw boekhouder u iets anders vraagt.

### 4.5 Het rekeningenstelsel

**Chart of accounts** toont het rekeningenstelsel als een boom — klassen, groepen, rekeningen — met het
saldo van elke rekening op het einde van het getoonde boekjaar. Eronder de **historiek** van de gekozen
rekening: haar lijnen van het boekjaar, het overgedragen saldo en het lopend saldo.

![Het rekeningenstelsel](images/chart.png)
*Het rekeningenstelsel: de rekeningen die bij "7001" passen; de historiek van rekening 700100, de verkoop van diensten van het boekjaar.*

- **With movements** toont alleen de rekeningen die dit jaar werden gebruikt; **All** het volledige
  stelsel.
- Het zoekvak vindt een rekening aan de hand van het begin van haar nummer (`7001`) of woorden uit haar
  naam (`leasing`).
- Dubbelklik op een klasse of een groep om ze in of uit te klappen.
- Dubbelklik op een lijn van de historiek om haar document te openen.

**New account** voegt een rekening toe: haar **nummer** (meestal 6 cijfers, klassen 0 tot 7, nog niet in
het stelsel) en haar **naam**. **Edit** wijzigt de gekozen rekening:

![Een rekening](images/account-dialog.png)
*De fiche van een rekening: haar naam, de aard van haar aankopen, verborgen of niet.*

- **Name**: de naam.
- **Purchases**: de aard van wat op deze rekening wordt aangekocht, die het rooster van de aankopen in
  de btw-aangifte bepaalt — **Goods** (handelsgoederen, rooster 81), **Services** (diensten en diverse
  goederen, rooster 82), **Investments** (investeringen, rooster 83), of **By its number** (volgens het
  nummer: 60 handelsgoederen, 61 diensten, 2x investeringen...).
- **Hidden (no longer offered)**: de rekening blijft in het stelsel en de rapporten maar wordt niet
  meer voorgesteld tijdens het typen.

### 4.6 Starten met een bestaande boekhouding

Als u tijdens het leven van een vennootschap overstapt op Ledger, neem de saldi dan over met één
**diverse verrichting** (hoofdstuk 10) gedateerd op de **eerste dag** van uw eerste boekjaar in Ledger,
en vink **Opening balances** (openingssaldi) aan:

- een lijn per balansrekening met haar saldo — het kapitaal (100000) en de reserves in het credit, het
  materieel (2x) en de bank (550000) in het debet...;
- een lijn **per openstaande factuur** op 400000 met haar klant (debet) of op 440000 met haar leverancier
  (credit), zodat ze later met haar betaling kan worden afgepunt;
- het eventuele verschil op 499000 (de wachtrekening), weg te werken met uw boekhouder.

Neem de cijfers uit de slotbalans en de openstaande posten van de vorige boekhouding.

## 5. Klanten en leveranciers

### 5.1 De lijsten

**Customers** en **Suppliers** tonen de derden met hun **code**, **naam**, **btw-nummer**, **gemeente**,
**saldo** en wat **achterstallig** is. De filters tonen **All** (alle), die **With a balance** (met een
saldo) of die **Overdue** (achterstallig); het zoekvak vindt een code, een naam, een gemeente, een
btw-nummer of een e-mail.

![De klanten](images/customers.png)
*De klanten; eronder de rekening van de gekozen klant: haar facturen en betalingen, afgepunt (de schakel) of open.*

Onder de lijst staat de **rekening** van de gekozen derde voor het getoonde boekjaar: het overgedragen
saldo, elke factuur, creditnota en betaling, hun **vervaldag** (in het rood als ze voorbij is) en het
**lopend saldo** (D: de derde is u iets verschuldigd, C: u bent de derde iets verschuldigd). Een schakel
markeert de lijnen die al **afgepunt** zijn. Dubbelklik op een lijn om haar document te openen.

### 5.2 De fiche van een derde

Klik op **New customer** (of **New supplier**), dubbelklik op een derde, of kies hem en klik op
**Card**. Een nieuwe derde kan ook tijdens het invoeren van een document worden aangemaakt: de knop **+**
rechts van zijn veld.

![De fiche van een klant](images/customer-card.png)
*De fiche van een klant: code, betalingstermijn, btw-nummer en -stelsel, taal, adres, bank, gebruikelijke rekening.*

- **Name** en een **Code** — gemaakt uit de naam als hij leeg blijft (*BROUWERI*); u mag een eigen code
  typen.
- **Payment terms**: het aantal dagen tot de vervaldag van een factuur (standaard 30). De vervaldag van
  de facturen volgt eruit.
- **VAT number**: gecontroleerd tijdens het typen — **Valid Belgian number**, **EU number (its form)**
  (Europees nummer, de vorm), **Its check digits are wrong** (foute controlecijfers). Het typen ervan
  vult ook het **btw-stelsel** en het **land** in.
- **VAT situation**: het btw-stelsel van de derde, dat de btw-codes op zijn documenten bepaalt
  (paragraaf 6.3):
  - **Belgian, subject to VAT**: Belgische btw-plichtige, de gewone tarieven (V21, A21...);
  - **Private person (no VAT number)**: een particulier, de gewone tarieven; hij komt niet in de
    klantenlisting;
  - **Other EU country, VAT number**: intracommunautair (VEUS diensten, VEUG goederen voor een klant;
    AEUS21, AEU21 voor een leverancier);
  - **Outside the EU**: buiten de EU — uitvoer (VEX), of diensten uit het buitenland (AWS21);
  - **Belgian co-contractor (reverse charge)**: Belgische medecontractant — werken in onroerende staat,
    de klant voldoet de btw (VCC, ACC21).
- **Language**: **French**, **Dutch** of **English** — de taal van de afgedrukte offertes, bestellingen
  en facturen van de derde.
- **Street**, **Postcode**, **City**, **Country** (de landcode: `BE`, `FR`, `NL`...).
- **E-mail**, **Phone**.
- **IBAN** en **BIC**: de rekening van een leverancier is nodig om hem via SEPA te betalen (hoofdstuk 8);
  die van een klant laat de CODA-import zijn betalingen herkennen.
- **Account**: de rekening die op de lijnen van zijn documenten wordt voorgesteld (een klant: 700100
  *diensten in België*; een leverancier: 612000 *huur*...).
- **VAT code**: een eigen btw-code, anders **By its situation** (volgens het stelsel).
- **Notes**: alles wat u wilt onthouden.

Om een derde te verwijderen, kiest u hem en drukt u op `Delete`. Een derde met documenten, offertes of
bestellingen kan niet worden verwijderd: zijn fiche blijft met hen in de boekhouding.

### 5.3 Afpunten

Ledger punt een factuur af met haar betaling wanneer u de betaling boekt (paragraaf 9.2): meestal hoeft u
niets te doen. Om zelf af te punten — een factuur betaald in twee delen, een creditnota die een factuur
compenseert:

1. Vink in de rekening van de derde de lijnen aan (klik op hun vakje, of `Space`). De knop **Match**
   toont de som van de aangevinkte lijnen.
2. Als die **nul** is, klikt u op **Match**: de lijnen krijgen een schakel en de factuur staat als betaald
   (**Paid**).

**Unmatch** maakt het afpunten van de gekozen lijn ongedaan. **Open items only** toont alleen de lijnen
die nog niet zijn afgepunt, van alle boekjaren.

## 6. Verkopen

### 6.1 De verkopen

**Sales** toont de verkoopfacturen en -creditnota's van het boekjaar: hun **nummer**, **datum**,
**klant**, **omschrijving**, **totaal** en **toestand** (**Status**).

![De verkopen](images/sales.png)
*De verkopen van 2026: betaald, te vervallen, achterstallig, een vereffende creditnota.*

| Toestand | Betekenis |
|---|---|
| **Paid** | De factuur is afgepunt met haar betaling |
| **Due 26/10** | Open, vervalt op die dag |
| **4 days late** | Open en voorbij de vervaldag (hier 4 dagen) |
| **Settled** | Een creditnota die is afgepunt (met haar factuur of een terugbetaling) |
| **Credit open** | Een creditnota die nog niet werd gebruikt |

Een creditnota krijgt **CN** na haar nummer. **All**, **Open**, **Overdue** en **Paid** filteren de
lijst; als een soort document meerdere dagboeken heeft, kiest een lijst er een van of **All the
journals** (alle).

### 6.2 Een verkoopfactuur, stap voor stap

Klik op **New invoice** (`Ctrl+N`), of **Invoice** in het overzicht, of **Documents ▸ Sales Invoice**.

![Een nieuwe verkoopfactuur, tijdens het invoeren](images/invoice-parts.png)
*Een verkoopfactuur tijdens het invoeren: twee lijnen aan 21 %; eronder de boeking die ze zal maken en haar totalen.*

1. **Customer** (klant): typ enkele letters van haar naam, haar code of haar btw-nummer en neem haar uit
   de lijst (of klik op **+** om een fiche aan te maken). Haar adres en btw-nummer verschijnen eronder.
2. **Date** en **Due date** (vervaldag): vandaag (of de dichtstbijzijnde dag in het boekjaar), en de
   datum plus de betalingstermijn van de klant; het aantal dagen wordt getoond.
3. **Description** (omschrijving): waarvoor de factuur is (*Bieretiketten: ontwerp en proeven*); ze
   verschijnt in de lijsten en op de afgedrukte factuur.
4. **Kind**: **Invoice** (factuur) of **Credit note** (creditnota).
5. **Communication**: de Belgische **gestructureerde mededeling** (+++123/4567/89012+++) die de klant
   bij zijn betaling vermeldt. Laat ze leeg: Ledger maakt ze bij het opslaan uit het nummer van de
   factuur, en drukt ze af op de factuur. De CODA-bestanden van uw bank geven ze terug met de betaling,
   die zo haar factuur terugvindt.
6. **Reference**: het bestelnummer of de referentie van de klant, afgedrukt op de factuur.
7. **De lijnen**: voor elke lijn de **rekening** (de gebruikelijke rekening van de klant wordt
   voorgesteld: 700100...), een **omschrijving**, het bedrag **zonder btw** (**Excl. VAT**) en de
   **btw-code** (voorgesteld volgens het btw-stelsel van de klant, paragraaf 6.3). De **btw** wordt
   berekend — typ een ander bedrag als de factuur iets anders vermeldt (het wordt dan in het blauw
   getoond) —, net als het **totaal** van de lijn.
8. **De boeking die ze maakt** (**The entry it makes**): de klant gedebiteerd, de verkopen en de btw
   gecrediteerd — zoals ze zal worden geboekt.
9. **De totalen**: zonder btw, de btw per code, het **te betalen totaal** (**Total to pay**).
10. **Save** boekt de factuur en keert terug naar de lijst; **Save & New** boekt ze en begint de
    volgende.

De factuur krijgt het volgende nummer van het dagboek (*VKP 2026/0062*). Ze staat voortaan in de
rekening van de klant, in de btw-aangifte van haar periode en in alle rapporten.

![Een opgeslagen factuur, achterstallig](images/invoice.png)
*Een opgeslagen factuur: "Open, overdue since 24/09/2026" (open, achterstallig sinds 24/09/2026), haar gestructureerde mededeling; Print maakt ze in Letters.*

### 6.3 De btw-codes van de verkopen

| Code | Te gebruiken voor | Btw-aangifte |
|---|---|---|
| **V21**, **V12**, **V6** | Verkopen in België aan 21, 12 of 6 % | Roosters 03, 02, 01 en 54 |
| **V0** | Verkopen aan 0 % (kranten...) | Rooster 00 |
| **VCC** | Werken voor een Belgische medecontractant: de klant voldoet de btw | Rooster 45 |
| **VEUG** | Goederen aan een onderneming in een ander EU-land | Rooster 46, intracommunautaire opgave |
| **VEUS** | Diensten aan een onderneming in een ander EU-land | Rooster 44, intracommunautaire opgave |
| **VEUT** | Driehoeksverkeer (ABC) binnen de EU | Rooster 46, intracommunautaire opgave |
| **VEX** | Uitvoer buiten de EU | Rooster 47 |
| **VX** | Vrijgestelde handelingen (artikel 44), buiten de aangifte | — |

De code wordt voorgesteld volgens het **btw-stelsel** van de klant, of de eigen **btw-code** op zijn
fiche. Creditnota's gaan naar roosters 48 (intracommunautair) en 49 (andere), hun btw naar rooster 64.
Bijlage A geeft alle codes.

> **Opmerking.** Op een afgedrukte factuur draagt het btw-detail de wettelijke vermelding die haar codes
> vragen — intracommunautaire verlegging (art. 39bis, art. 21 § 2), verlegging naar de medecontractant
> (art. 20 KB nr. 1), uitvoer (art. 39), vrijstelling (art. 44) —, in de taal van de factuur.

### 6.4 Een factuur afdrukken

Klik op een opgeslagen factuur op **Print** — of klik er in de lijst met de rechtermuisknop op en kies
**Print**. Ledger geeft haar gegevens door aan **Letters**, dat de factuur uit haar sjabloon maakt en
toont: druk ze af, of sla ze opnieuw op als `.docx` of `.odt`. Zie hoofdstuk 12.

![Een factuur afgedrukt door Letters](images/invoice-printed.png)
*Dezelfde factuur afgedrukt: in het Nederlands, de taal van de klant — "FACTUUR".*

### 6.5 Creditnota's

Een creditnota annuleert een factuur geheel of gedeeltelijk. Klik in de lijst met de rechtermuisknop op
de factuur en kies **Make a credit note for it**: er opent een creditnota met dezelfde klant en lijnen en
de omschrijving *Creditnota voor VKP 2026/0054*; wijzig de bedragen als slechts een deel wordt
gecrediteerd, en sla op.

![Het snelmenu van de verkopen](images/sales-menu.png)
*Rechtsklikken op een verkoop: openen, afdrukken, er een creditnota voor maken, verwijderen.*

U kunt ook bovenaan de lijst op **Credit note** klikken voor een vrije creditnota. Punt ze na het
opslaan af met haar factuur in de rekening van de klant (paragraaf 5.3) als ze die vereffent.

### 6.6 Een verkoop wijzigen

Open een verkoop, wijzig ze en sla op: haar boeking wordt opnieuw geboekt, onder hetzelfde nummer. Haar
betaling blijft afgepunt zolang haar totaal niet verandert. Een verkoop kan niet meer worden gewijzigd
zodra haar btw-aangifte is ingediend of haar boekjaar afgesloten (paragraaf 3.4).

## 7. Aankopen

### 7.1 De aankopen

**Purchases** toont de aankoopfacturen en -creditnota's van het boekjaar, zoals de verkopen, met het
**nummer van de leverancier** (**Their number**) in plaats van de omschrijving.

![De aankopen](images/purchases.png)
*De aankopen van 2026: er zijn er nog twee te betalen.*

De toestanden zijn die van de verkopen, plus **Transfer sent** (overschrijving verstuurd): de factuur zit
in een SEPA-betalingsbestand (hoofdstuk 8) en wacht op het bankuittreksel dat ze betaalt.

### 7.2 Een aankoopfactuur

Klik op **New purchase**, of **Purchase** in het overzicht, of **Documents ▸ Purchase Invoice**. U voert
ze in zoals een verkoopfactuur (paragraaf 6.2), met deze verschillen:

- **Their number**: het factuurnummer van de leverancier (*F2026-4569*). Ledger waarschuwt u als dezelfde
  leverancier dit boekjaar al een factuur met dat nummer heeft: een dubbel geboekte aankoop.
- **Communication**: de gestructureerde mededeling op de factuur van de leverancier, als die er een
  heeft. Ledger controleert de cijfers; de SEPA-betaling gebruikt ze.
- De **rekening** van elke lijn: een kost (60 handelsgoederen, 61 diensten, 62 bezoldigingen...) of, voor
  materieel, een vast actief (2x). De gebruikelijke rekening van de leverancier wordt voorgesteld.
- De **btw-code**: zie paragraaf 7.3. Bij verlegging van heffing toont de boeking beide zijden: de
  verschuldigde btw en dezelfde aftrekbare btw.

![Een aankoopfactuur: een autolease](images/purchase.png)
*Een autolease: btw-code A21D50 — de helft van de btw aftrekbaar (58,80), de andere helft bij de kost gevoegd.*

### 7.3 De btw-codes van de aankopen

| Code | Te gebruiken voor | Btw-aangifte |
|---|---|---|
| **A21**, **A12**, **A6** | Aankopen in België, btw aftrekbaar | Roosters 81/82/83 en 59 |
| **A0** | Aankopen aan 0 % of vrijgesteld | Roosters 81/82/83 |
| **A21D50** | Een bedrijfswagen: de helft van de btw aftrekbaar | Roosters 81/82/83 en 59 |
| **A21ND**, **A12ND** | Niet-aftrekbare btw (restaurants, geschenken...): bij de kost gevoegd | Roosters 81/82/83 |
| **AEU21**, **AEU12**, **AEU6** | Goederen gekocht in een ander EU-land | Roosters 86, 55 en 59 |
| **AEUS21** | Diensten gekocht in een ander EU-land | Roosters 88, 55 en 59 |
| **ACC21**, **ACC12**, **ACC6** | Werken van een Belgische medecontractant | Roosters 87, 56 en 59 |
| **AWS21** | Diensten van buiten de EU | Roosters 87, 56 en 59 |
| **AIM21** | Invoer, btw verlegd (vergunning ET 14000) | Roosters 87, 57 en 59 |

Het rooster van de aankoop zelf — **81** handelsgoederen, **82** diensten, **83** investeringen — volgt
de rekening van de lijn (haar aard **Purchases**, paragraaf 4.5). Ontvangen creditnota's gaan naar
roosters 84 of 85, hun btw naar rooster 63.

### 7.4 Aankoopcreditnota's

Klik op **Credit note**, of klik met de rechtermuisknop op een factuur en kies **Make a credit note for
it**. Typ het nummer van de creditnota van de leverancier in **Their number**.

## 8. Leveranciers betalen (SEPA)

Ledger schrijft het **SEPA-overschrijvingsbestand** (het ISO 20022-formaat *pain.001* dat alle Belgische
banken aanvaarden) dat meerdere facturen in één keer betaalt: laad het op de site van uw bank op en
bevestig daar.

1. Klik in **Purchases** op **Pay...** (of kies **Tools ▸ Pay Suppliers (SEPA)...**).
2. Kies de rekening **From the account** (van waar betaald wordt: een bankdagboek met zijn IBAN) en de dag
   **Paid on** (waarop de bank betaalt).
3. Vink de te betalen facturen aan. Die welke binnen de week van die dag vervallen, zijn al voor u
   aangevinkt. Een leverancier zonder IBAN op zijn fiche toont **No IBAN** en kan niet worden aangevinkt:
   vul zijn fiche aan. Een factuur die al in een bestand zit, toont **In a file**.
4. Controleer het aantal overschrijvingen en hun totaal, en klik op **Make the file**.

![De leveranciers betalen](images/pay-suppliers.png)
*Twee facturen aangevinkt: twee overschrijvingen, samen 2.960,87.*

Het bestand wordt geschreven in `SD:/docs/Payments`, genoemd naar de dag (*SEPA 2026-09-28
123400.xml*). Elke overschrijving draagt de naam, het adres, het IBAN en de BIC van de leverancier, en
zijn gestructureerde mededeling — anders het factuurnummer van de leverancier. De facturen tonen dan
**Transfer sent**, tot het bankuittreksel dat ze betaalt is geboekt (ingevoerd of geïmporteerd): dan
worden ze afgepunt.

## 9. Bank en kas

### 9.1 De uittreksels

**Bank and cash** toont de bank- en kasuittreksels van het boekjaar: hun nummer, datum, omschrijving,
dagboek, het geld dat binnenkwam (**In**) en buitenging (**Out**), en het **nieuwe saldo** (**New
balance**).

![De uittreksels](images/bank.png)
*De bankuittreksels van 2026, hun ontvangsten en uitgaven, en het saldo na elk ervan.*

### 9.2 Een uittreksel invoeren

Klik op **New statement** (of **Statement** in het overzicht, of **Documents ▸ Bank Statement**).

1. **Journal**: het bank- of kasdagboek. Het **oude saldo** — het saldo na het laatste uittreksel — wordt
   getoond.
2. **Date**: de datum van het uittreksel. **Description**: het nummer na het laatste uittreksel van het
   dagboek wordt voorgesteld (*Statement 43* na *Statement 42*).
3. **New balance**: het nieuwe saldo dat op het uittreksel van uw bank staat. Facultatief, maar
   aanbevolen: Ledger controleert dan de verrichtingen en toont **Balanced** (sluitend), of **Off by**
   (verschil van) het verschil.
4. De **verrichtingen**, een per lijn:
   - **Party or account**: de klant of leverancier die betaalde of werd betaald (typ zijn naam of code),
     of een rekening voor al het andere — bankkosten 657200, de btw betaald aan de Staat 451200, een
     bezoldiging 618000;
   - **Description**: de mededeling van de bank;
   - **Amount**: positief voor geld dat **binnenkomt**, negatief voor geld dat **buitengaat**
     (`-238,37`).
5. Als een verrichting een derde heeft, verschijnen eronder zijn **openstaande posten** — zijn nog niet
   betaalde facturen. **Vink aan** welke de verrichting betaalt: het bedrag volgt. Typt u eerst het
   bedrag, dan wordt de post aangevinkt die evenveel bedraagt. En een **gestructureerde mededeling**
   getypt in de omschrijving vindt vanzelf haar factuur.
6. **Save**. De aangevinkte posten worden afgepunt met hun betaling: de facturen staan als betaald.

![Een opgeslagen uittreksel](images/statement.png)
*Een uittreksel: de factuur van elke betaling in "Pays"; eronder de post die een verrichting betaalde, aangevinkt.*

Een opgeslagen uittreksel kan opnieuw worden geopend: de kolom **Pays** toont de factuur die elke
verrichting betaalde, en de lijst van posten toont ze aangevinkt.

### 9.3 CODA-bestanden importeren

Belgische banken leveren hun uittreksels als **CODA**-bestanden (*gecodeerde rekeninguittreksels*, de
norm van Febelfin). Download ze uit uw online bankieren — vaak onder *Documenten* of *CODA*; sommige
banken sturen ze naar uw boekhouder, die ze aan u kan doorgeven — en kopieer ze naar de SD-kaart.

1. Klik in **Bank and cash** op **Import CODA** (of **Tools ▸ Import CODA...**) en kies het bestand.
2. Ledger leest de uittreksels — een bestand kan er meerdere bevatten —, vindt het **dagboek van elk aan
   de hand van zijn IBAN** en slaat die over die al in uw boekhouding staan.
3. Elk uittreksel opent, **al ingevuld**:
   - een betaling met een **gestructureerde mededeling** vindt haar factuur — verkoop of aankoop —, die
     wordt aangevinkt;
   - anders wordt de tegenpartij gevonden aan de hand van haar **IBAN** (dat van een fiche) of haar
     **naam**, en wordt haar openstaande post met hetzelfde bedrag aangevinkt (of al haar posten, als ze
     samen het bedrag vormen);
   - de **bankkosten** gaan naar 657200;
   - wat niet werd gevonden, toont **To complete** (aan te vullen) in het rood: typ de derde of de
     rekening.
4. De titelregel van de pagina zegt wat overblijft: *CODA: 1 movement to complete* (1 verrichting aan te
   vullen). Vul ze aan en klik op **Save**: het uittreksel wordt geboekt en het volgende van het bestand
   verschijnt, tot het laatste.

![Een geïmporteerd CODA-uittreksel](images/coda.png)
*Het CODA-bestand van de demo geïmporteerd: vijf verrichtingen gevonden (hun facturen aangevinkt), één aan te vullen.*

> **Tip.** Probeer het op de demo: **Import CODA** ▸ `demo-bank-statement.cod` ▸ **Open**. De 250,00 van
> Janssens Pieter komen niet van een gekende derde: typ een rekening (een diverse opbrengst, bijvoorbeeld)
> of maak een fiche voor hem met **+**.

Meldt Ledger **No bank journal has the account BE..** (geen bankdagboek heeft deze rekening), typ dat
IBAN dan in het bankdagboek (**Settings ▸ Journals**) en importeer het bestand opnieuw. Meldt het dat
**het oude saldo van de bank niet dat van de boekhouding is** (*the bank's old balance is not the
books'*), dan ontbreekt er een uittreksel tussen het laatste in uw boekhouding en dit: importeer dat
eerst.

### 9.4 De kas

Een **kasuittreksel** (**Documents ▸ Cash Statement**, of **New statement** met het kasdagboek gekozen)
voert u in zoals een bankuittreksel: contante verkopen binnen, kleine aankopen buiten, de verrichtingen
van de dag of de maand.

## 10. Diverse verrichtingen

**Misc. operations** bewaart de boekingen die geen facturen of uittreksels zijn: afschrijvingen, lonen
geboekt volgens het overzicht van uw sociaal secretariaat, overlopende rekeningen, verbeteringen,
openingssaldi, en de btw-verrekeningen die Ledger zelf boekt (paragraaf 14.3).

![De diverse verrichtingen](images/misc.png)
*De diverse verrichtingen van 2026: de btw-verrekeningen van het eerste en tweede kwartaal.*

Klik op **New operation**:

1. **Journal**, **Date** en **Description** (*Afschrijving materieel 2026*).
2. De lijnen: de **rekening**, de **derde** (**Party**) als de rekening een klanten- of
   leveranciersrekening is (een lijn op 400000 heeft haar klant nodig), een **omschrijving**, het
   **debet** of het **credit**, en een btw-code als de lijn in de btw-aangifte moet meetellen (een
   regularisatie: codes R61, R62).
3. De titelregel toont het totale **Debit**, **credit** en het verschil (**difference**). **Balance it on
   this line** zet het verschil op de lijn waarin u staat.
4. **Save** als ze **sluit** (*balanced*).

![Een nieuwe diverse verrichting](images/misc-new.png)
*Een afschrijving: 630200 gedebiteerd, 230009 gecrediteerd, elk 1.425,00 — sluitend.*

Vink **Opening balances** aan voor de boeking die de saldi van uw vorige boekhouding overneemt
(paragraaf 4.6); ze wordt in de lijst gemarkeerd als **Opening**.

## 11. Offertes, bestellingen en leveringsbonnen

### 11.1 De commerciële documenten

**Quotes and orders** bewaart de documenten die aan een factuur voorafgaan. Ze worden **niet geboekt**:
ze veranderen noch de rekeningen noch de btw.

| Document | Wat het is |
|---|---|
| **Quote** | Een offerte: een prijs aangeboden aan een klant, geldig tot een datum (standaard 30 dagen) |
| **Order** | Een bestelling van een klant (gemaakt uit haar offerte, of getypt) met haar leverdatum |
| **Delivery note** | Een leveringsbon: wat werd geleverd, te laten tekenen door de klant |
| **Purchase order** | Een bestelbon: wat u bij een leverancier bestelt |

Elke soort wordt per jaar genummerd: *Quote 2026/0003*.

![De offertes en bestellingen](images/quotes.png)
*De offertes, bestellingen, leveringsbonnen en bestelbonnen van het jaar, en hun toestand.*

De filters tonen **All** (alle) of één soort; de **toestand** (**State**) zegt hoe ver elk document
staat:

| Toestand | Betekenis |
|---|---|
| **Draft** | Ontwerp, in voorbereiding |
| **Sent** | Verstuurd naar de klant (of de leverancier) |
| **Accepted** | De klant zei ja |
| **Refused** | De klant zei nee |
| **Expired** | Een offerte waarvan de geldigheid voorbij is, noch aanvaard noch geweigerd |
| **Ordered**, **Delivered**, **Invoiced** | Afgehandeld: besteld, geleverd, gefactureerd — wat erop volgde |

### 11.2 Een offerte

Klik op **New quote** (of **Other...** voor een bestelling, een leveringsbon of een bestelbon; of het menu
**Documents**).

![Een offerte](images/quote.png)
*Een offerte: drie lijnen — hoeveelheid maal eenheidsprijs —, haar btw en totaal.*

1. De **klant**, de **datum**, **Valid until** (geldig tot, een offerte) of **Delivery** (levering, de
   andere), een **omschrijving** — de titel van de offerte op papier (*Le packaging de Noël*) —, de
   **toestand** en de **referentie** van de klant.
2. De lijnen: een **omschrijving**, de **hoeveelheid** (`2,5` uur), de **eenheidsprijs** zonder btw en de
   **btw-code**; het **totaal** wordt berekend.
3. **Save**, en dan **Print** om ze in Letters te maken (hoofdstuk 12).

![Een offerte afgedrukt door Letters](images/quote-printed.png)
*De offerte afgedrukt vanuit haar Franse sjabloon: het briefhoofd, de klant, de lijnen, de totalen.*

### 11.3 De volgende stap

**Next step** zet het document om in het volgende:

![De volgende stap](images/quote-next.png)
*De volgende stappen van een offerte: de bestelling, een leveringsbon, de factuur; aanvaard, geweigerd.*

- **Make the order** (een offerte): een bestelling met dezelfde klant en lijnen; de offerte wordt als
  aanvaard gemarkeerd.
- **Make a delivery note** (een offerte of een bestelling): een leveringsbon.
- **Make the invoice**: de pagina van de verkoopfactuur opent, ingevuld — elke lijn *2,5 x Design...*
  met haar totaal, op de gebruikelijke rekening van de klant. Controleer ze en klik op **Save**: ze wordt
  geboekt en het document wordt gemarkeerd als **Invoiced** (gefactureerd).
- **Accepted** en **Refused** (een offerte) registreren het antwoord van de klant.

Een document dat uit een ander werd gemaakt, vermeldt dat op papier (*Naar aanleiding van offerte
2026/0001*). De volgende stap van een **bestelbon** maakt de aankoopfactuur, aan te vullen met het
nummer van de leverancier.

## 12. Documenten afdrukken vanuit sjablonen

### 12.1 Hoe het werkt

**Print** — op een offerte, een bestelling, een leveringsbon, een bestelbon, een verkoopfactuur of
-creditnota — schrijft de gegevens van het document en vraagt **Letters** het document te maken vanuit zijn
**sjabloon**. Letters toont het daarna: druk het af, of sla het opnieuw op als `.docx` of `.odt`.

De gemaakte documenten worden bewaard in `SD:/docs`, een map per soort — **Quotes**, **Orders**,
**Delivery notes**, **Purchase orders**, **Invoices**, **Credit notes** —, genoemd naar hun nummer en hun
derde (*Quote 2026-0002 Chocolaterie Van Hove SRL.rtf*).

### 12.2 Talen

Elk sjabloon bestaat in het **Frans**, het **Nederlands** en het **Engels**. De documenten van een derde
nemen de taal van zijn fiche, anders die van de vennootschap (die van haar rekeningenstelsel). De woorden
(*Facture*, *Factuur*, *Invoice*), de datums en de wettelijke vermeldingen volgen.

### 12.3 Uw eigen sjablonen

Een sjabloon is een gewoon Letters-document — `.rtf`, `.docx` of `.odt` — waarvan Ledger de
**samenvoegvelden** invult. Ze staan in `SD:/apps/ledger.app/templates`: de Franse in die map, de
Nederlandse in `nl`, de Engelse in `en`, een per soort: `quote` (offerte), `order` (bestelling),
`delivery` (levering), `porder` (bestelbon), `invoice` (factuur), `creditnote` (creditnota).

![Settings, afdrukken](images/settings-printing.png)
*De sjablonen van elke taal: een eigen sjabloon, of het Franse dat in de plaats wordt gebruikt.*

Kies in **Settings ▸ Printing** de taal en het document en klik op **Edit in Letters**. Wijzig de opmaak,
de woorden, de voorwaarden; voeg uw logo toe (**Insert ▸ Image...** in Letters); verplaats de velden. Als
een taal geen eigen sjabloon heeft (**French one**), stelt Ledger voor het uit het Franse te maken.
**Open the folder** toont de sjablonen in de File Viewer.

Om een veld toe te voegen, gebruikt u **Tools ▸ Mail Merge** in Letters: het toont alle velden van
Ledger, met de waarden van een voorbeeld. Een **tabelrij** die lijnvelden bevat (**LineText**,
**LineQty**...) wordt herhaald voor elke lijn van het document. Bijlage B geeft alle velden.

> **Tip.** Bewaar een kopie van een sjabloon voor u het wijzigt. Raakt er een beschadigd, dan krijgt u de
> oorspronkelijke sjablonen terug met een nieuwe kopie van de map `apps/ledger.app/templates` van de
> Onyx-kaart.

## 13. Rapporten

### 13.1 Een rapport kiezen

**Reports** toont een rapport op het scherm; **Letters** opent het als een document om af te drukken (A4,
liggend als het breed is, met titel en paginanummers), **Spreadsheet** als een werkmap, en **Save
as...** schrijft het als Letters-document (`.rtf`), werkmap (`.xlsx`) of CSV-bestand — standaard in
`SD:/docs/Reports`.

![Een rapport kiezen](images/reports-list.png)
*De rapporten.*

Kies het **rapport**, dan zijn **periode**: **Year** (het boekjaar), **Q1** tot **Q4** (de kwartalen),
**Month** (de lopende maand), of datums getypt in **From** en **to** (de saldi: **At**, op een datum).
Dubbelklik op een lijn om haar document, haar rekening of haar derde te openen.

### 13.2 De rapporten één voor één

| Rapport | Wat het toont | Opties |
|---|---|---|
| **Journals** (dagboeken) | Elk document van de periode met de lijnen van zijn boeking | Eén dagboek, of alle |
| **General ledger** (grootboek) | De lijnen van elke rekening, het overgedragen saldo en het lopend saldo | Een reeks rekeningen; ook nulsaldi |
| **Trial balance** (proef- en saldibalans) | Het debet, credit en saldo van elke rekening op een datum | Ook nulsaldi |
| **Balance sheet** (balans) | De activa en passiva, in de rubrieken van het verkorte schema | Op een datum |
| **Income statement** (resultatenrekening) | De opbrengsten en kosten, het resultaat van de periode | — |
| **Customers' balances**, **Suppliers' balances** | Het debet, credit, saldo en achterstallige bedrag van elke derde | Op een datum |
| **Receivables by age**, **Payables by age** | De openstaande posten volgens ouderdom: niet vervallen, 1–30, 31–60, 61–90, meer dan 90 dagen | Op een datum |
| **A party's account** (rekening van een derde) | De lijnen van een klant of leverancier, met vervaldagen en afpunting | De derde |
| **VAT detail** (btw-detail) | De lijnen achter elk rooster van de btw-aangifte | — |

![Het grootboek](images/reports.png)
*Het grootboek van 2026: van elke rekening het overgedragen saldo, de lijnen en het lopend saldo.*

![De balans](images/balance-sheet.png)
*De balans op 31/12/2026, in de rubrieken van het verkorte schema.*

![De resultatenrekening](images/income-statement.png)
*De resultatenrekening van 2026: omzet, kosten per rubriek, resultaat.*

![Vorderingen volgens ouderdom](images/receivables.png)
*Wat de klanten verschuldigd zijn, volgens ouderdom.*

![Een rapport in Letters](images/report-letters.png)
*De balans geopend in Letters, klaar om af te drukken.*

## 14. Btw

### 14.1 De btw-pagina

**VAT** toont de btw-aangiften van een kalenderjaar: de periodes — kwartalen of maanden — als tegels, en
de aangifte van de gekozen periode zoals het formulier ze voorstelt.

![De btw-aangiften](images/vat.png)
*Het derde kwartaal van 2026: de roosters van de aangifte, berekend uit de boekhouding; de controles vinden niets.*

De toestand van elke periode:

| Toestand | Betekenis |
|---|---|
| **Filed** | Als ingediend gemarkeerd (op die dag): haar btw-boekingen zijn vergrendeld |
| **Running** | De periode loopt nog |
| **To file** | Voorbij, haar aangifte is verschuldigd (de 20ste van de volgende maand) |
| **Late** | Haar vervaldag is voorbij |
| **To come** | Nog niet begonnen |

Klik op een tegel — of gebruik `←` en `→` — om een andere periode te zien. De aangifte wordt berekend uit
de documenten van de periode, in de vakken van het formulier: **II** de handelingen (verkopen, roosters
00 tot 49), **III** de aankopen (81 tot 88), **IV** de verschuldigde btw (54 tot 63, totaal **XX**),
**V** de aftrekbare btw (59, 62, 64, totaal **YY**), **VI** het saldo: **71** verschuldigd aan de Staat,
of **72** verschuldigd door de Staat. Klik op een rooster, of op **Detail**, om de lijnen achter de
roosters te zien (het rapport **VAT detail** van de periode).

Onder het formulier staan **de controles die Intervat uitvoert** — verschuldigde btw zonder
maatstaf, een creditnota zonder handeling... Vinden ze iets, controleer het dan voor u indient.

### 14.2 De aangifte indienen

1. Controleer de roosters (en hun **Detail**).
2. Vink **Ask for the refund** (terugbetaling vragen) aan als rooster 72 een bedrag toont dat de Staat u
   verschuldigd is en u het terug wilt; **Ask for payment forms** als u de betaalformulieren wilt.
3. Klik op **Intervat XML** en sla het bestand op (in `SD:/docs/VAT`, *VAT return 2026-Q3.xml*).
4. Laad het bestand op in **Intervat** (intervat.minfin.fgov.be) en verstuur de aangifte.
5. Antwoord **Yes** als Ledger vraagt of de periode als ingediend moet worden gemarkeerd.

Bij maandaangiften toont die van december rooster **91**, het voorschot betaald in december: typ het
daar.

### 14.3 Als ingediend markeren, de verrekening

**Mark as filed** vergrendelt de btw van de periode: haar documenten met btw kunnen niet meer worden
gewijzigd, en de roosters van de aangifte worden bewaard zoals ingediend. Ledger stelt dan voor de
**verrekening** (*settlement*) te boeken: een diverse verrichting op de laatste dag van de periode die de
verschuldigde btw (451000) en de aftrekbare btw (411000) overbrengt naar de **btw-rekening-courant** —
451200 wat u de Staat betaalt, 411200 wat hij u terugbetaalt.

Wanneer u de btw betaalt, boekt u de betaling in uw bankuittreksel op rekening **451200**.

**Reopen** ontgrendelt een periode die per vergissing als ingediend werd gemarkeerd. Een reeds verstuurde
aangifte moet dan ook in Intervat worden verbeterd.

### 14.4 De listings

**Listings** maakt de twee andere btw-bestanden:

![De listings](images/vat-listings.png)
*De jaarlijkse klantenlisting en de intracommunautaire opgave van de periode.*

- **Customer listing 2026 (XML)**: de jaarlijkse lijst van uw Belgische btw-plichtige klanten die voor
  250 EUR of meer kochten, in te dienen vóór 31 maart van het volgende jaar. Zonder zo'n klant is de
  listing "nihil": dat moet in de laatste aangifte van het jaar worden vermeld — Ledger doet het in het
  bestand van die aangifte.
- **Intra-community listing (XML)**: de opgave van uw intracommunautaire leveringen en diensten van de
  periode, per btw-nummer van de klant. Een klant zonder geldig Europees btw-nummer wordt weggelaten, en
  Ledger meldt het.

Laad ze eveneens op in Intervat.

### 14.5 Zonder btw-aangiften

Onder de **vrijstellingsregeling** dient u geen periodieke aangifte in; de klantenlisting blijft
verschuldigd. **Niet btw-plichtig**: geen enkel btw-bestand. De btw-pagina meldt het.

## 15. Het boekjaar afsluiten

### 15.1 Voor het afsluiten

- Voer alle documenten van het boekjaar in: verkopen, aankopen, alle bankuittreksels tot de laatste dag.
- Dien de btw-aangiften van het boekjaar in en markeer ze als ingediend.
- Boek met uw boekhouder de eindejaarsboekingen: afschrijvingen, overlopende rekeningen, voorzieningen,
  voorraden, belastingen.
- Controleer de **proef- en saldibalans**, de **balans** en de **resultatenrekening**, en bezorg ze aan
  uw boekhouder.

### 15.2 Afsluiten

Kies in **Settings ▸ Fiscal years** het boekjaar en klik op **Close the year...** (of **Tools ▸ Close the
Fiscal Year...**).

![Het boekjaar afsluiten](images/close-year.png)
*De afsluiting van 2026: de winst overgedragen met een boeking op de laatste dag.*

Ledger boekt de **resultaatverwerking** op de laatste dag van het boekjaar — een winst: 693000
gedebiteerd, 140000 gecrediteerd; een verlies: 141000 gedebiteerd, 793000 gecrediteerd —,
**vergrendelt** de boekingen van het boekjaar en voegt het volgende boekjaar toe als het nog niet
bestaat. De balansrekeningen lopen vanzelf door in het volgende boekjaar.

Als uw algemene vergadering het resultaat anders bestemt (een dividend, de wettelijke reserve), boek die
boekingen dan in het volgende boekjaar (of vóór het afsluiten, met de rekeningen 69x / 79x: Ledger
verwerkt dan alleen wat overblijft).

**Reopen the year** ontgrendelt een afgesloten boekjaar; de boeking van de resultaatverwerking blijft —
verwijder ze als het resultaat verandert, en sluit opnieuw af.

## 16. Bestanden en back-ups

| Waar | Wat |
|---|---|
| `SD:/docs/<vennootschap>.ledger` | De boekhouding van de vennootschap: alles in één bestand (u kiest naam en plaats) |
| `<vennootschap>.ledger.bak` | De vorige versie van de boekhouding, bij elke wijziging bewaard |
| `SD:/docs/Quotes`, `Orders`, `Delivery notes`, `Purchase orders`, `Invoices`, `Credit notes` | De afgedrukte documenten |
| `SD:/docs/Reports` | De rapporten geopend in Letters of het rekenblad |
| `SD:/docs/VAT` | De btw-aangiften en listings (XML) |
| `SD:/docs/Payments` | De SEPA-betalingsbestanden |
| `SD:/apps/ledger.app/templates` | De sjablonen (`nl`, `en`: de Nederlandse en Engelse) |
| `SD:/apps/ledger.app/last.txt` | De laatst geopende boekhouding |
| `SD:/apps/ledger.app/merge.card`, `merge-lines.card`, `merge.job` | De gegevens van het laatst afgedrukte document, voor Letters |

**Maak** regelmatig een **back-up** van uw `.ledger`-bestand — minstens na elke btw-aangifte — op een
USB-stick of een andere computer: het is uw hele boekhouding. **File ▸ Save a Copy As...** schrijft een
kopie waar u wilt.

Het `.ledger`-bestand is gewone tekst (Latin-1): een sectie `[company]`, daarna `[years]`, `[journals]`,
`[accounts]`, `[parties]`, `[entries]`, `[returns]` en `[documents]`, een tab tussen de cellen. Het kan
met elke teksteditor worden gelezen — en in geval van nood hersteld. Bewaar er een kopie van voor u eraan
raakt.

## 17. Vragen en antwoorden

**Ik kan een factuur niet wijzigen: ze toont "Locked".** Haar btw-aangifte is ingediend of haar boekjaar
afgesloten (paragraaf 3.4). Heropen de periode (**VAT ▸ Reopen**) of het boekjaar alleen als de aangifte
of de rekeningen nog niet werden verstuurd; boek anders nu een creditnota of een verbeterende
verrichting.

**"Only the journal's last sales document can be deleted".** Verkoopfacturen behouden een doorlopende
nummering. Maak een creditnota voor de factuur (rechtsklikken ▸ **Make a credit note for it**).

**"The date is in no fiscal year".** De datum valt in geen enkel boekjaar: voeg het boekjaar toe in
**Settings ▸ Fiscal years** (**Add the next year**), of verbeter de datum.

**"The fiscal year shown is closed: choose another one".** Nieuwe documenten gaan naar het boekjaar dat
in de zijbalk wordt getoond: kies daar het lopende boekjaar.

**Een betaalde factuur toont nog "Due" of "late".** Haar betaling werd geboekt zonder te worden
afgepunt. Vink in de rekening van de klant de factuur en de betaling aan en klik op **Match** (paragraaf
5.3).

**Het uittreksel sluit niet ("Off by...").** Er ontbreekt een verrichting of ze is verkeerd getypt:
vergelijk de lijnen met het uittreksel van de bank. Maak het **New balance** leeg om het toch op te
slaan.

**De CODA-import meldt "No bank journal has the account".** Typ het IBAN van die bankrekening in haar
dagboek (**Settings ▸ Journals**).

**De btw-controles vinden iets.** Open **Detail**, zoek het document, verbeter het (zijn btw-code, de
aard van zijn rekening) en kijk opnieuw.

**Het btw-nummer van een klant toont "Its check digits are wrong".** Controleer het op de Europese
VIES-site; Ledger behoudt het als u aandringt, maar Intervat zal het weigeren in de listings.

**Letters meldt "No template for this kind of document".** Er ontbreekt een sjabloon in
`SD:/apps/ledger.app/templates`: kopieer het terug van een nieuwe Onyx-kaart.

**Waar staat mijn factuur als bestand?** In `SD:/docs/Invoices`, genoemd naar haar nummer en klant.
Letters kan ze opnieuw opslaan als `.docx` of `.odt`.

**De stip onderaan de zijbalk is rood.** De boekhouding kon niet worden geschreven (kaart vol of tegen
schrijven beveiligd). Maak plaats vrij, en dan **File ▸ Save a Copy As...**.

## 18. Woordenlijst

De schermen van Ledger zijn in het Engels: hier de woorden die u er tegenkomt, met hun vertaling.

| English | Français | Nederlands |
|---|---|---|
| Account | Compte | Rekening |
| Accountant | Expert-comptable | Accountant, boekhouder |
| Balance sheet | Bilan | Balans |
| Chart of accounts (PCMN) | Plan comptable minimum normalisé | Minimum algemeen rekeningenstelsel (MAR) |
| Credit note | Note de crédit | Creditnota |
| Customer | Client | Klant |
| Customer listing | Listing clients | Klantenlisting |
| Debit, credit | Débit, crédit | Debet, credit |
| Delivery note | Bon de livraison | Leveringsbon |
| Depreciation | Amortissement | Afschrijving |
| Due date | Échéance | Vervaldag |
| Entry | Écriture | Boeking |
| Fiscal year | Exercice comptable | Boekjaar |
| General ledger | Grand livre | Grootboek |
| Income statement | Compte de résultats | Resultatenrekening |
| Invoice | Facture | Factuur |
| Journal | Journal | Dagboek |
| Matching | Lettrage | Afpunten |
| Miscellaneous operation | Opération diverse (OD) | Diverse verrichting |
| Open item | Poste ouvert | Openstaande post |
| Order | Commande, bon de commande | Bestelling, bestelbon |
| Purchase | Achat | Aankoop |
| Quote | Devis, offre | Offerte |
| Reverse charge | Autoliquidation | Verlegging van heffing |
| Sale | Vente | Verkoop |
| Statement | Extrait de compte | Rekeninguittreksel |
| Structured communication | Communication structurée | Gestructureerde mededeling |
| Supplier | Fournisseur | Leverancier |
| Trial balance | Balance des comptes | Proef- en saldibalans |
| VAT return | Déclaration TVA | Btw-aangifte |
| VAT number | Numéro de TVA | Btw-nummer |

## Bijlage A. De btw-codes

| Code | Naam | Tarief | Aftrekbaar | Roosters (factuur) | Roosters (creditnota) |
|---|---|---|---|---|---|
| V21 | Verkopen 21 % | 21 % | — | 03, 54 | 49, 64 |
| V12 | Verkopen 12 % | 12 % | — | 02, 54 | 49, 64 |
| V6 | Verkopen 6 % | 6 % | — | 01, 54 | 49, 64 |
| V0 | Verkopen 0 % | 0 % | — | 00 | 49 |
| VCC | Verkopen, btw verschuldigd door de Belgische medecontractant | — | — | 45 | 49 |
| VEUG | Intracommunautaire leveringen van goederen | — | — | 46 | 48 |
| VEUS | Intracommunautaire diensten | — | — | 44 | 48 |
| VEUT | Intracommunautair driehoeksverkeer (ABC) | — | — | 46 | 48 |
| VEX | Uitvoer buiten de EU | — | — | 47 | 49 |
| VX | Vrijgesteld (art. 44), buiten de aangifte | — | — | — | — |
| A21 | Aankopen 21 % | 21 % | 100 % | 81/82/83, 59 | 81/82/83, 85, 63 |
| A12 | Aankopen 12 % | 12 % | 100 % | 81/82/83, 59 | 81/82/83, 85, 63 |
| A6 | Aankopen 6 % | 6 % | 100 % | 81/82/83, 59 | 81/82/83, 85, 63 |
| A0 | Aankopen 0 % of vrijgesteld | 0 % | — | 81/82/83 | 81/82/83, 85 |
| A21D50 | Aankopen 21 %, half aftrekbaar (wagens) | 21 % | 50 % | 81/82/83, 59 | 81/82/83, 85, 63 |
| A21ND | Aankopen 21 %, niet aftrekbaar | 21 % | 0 % | 81/82/83 | 81/82/83, 85 |
| A12ND | Aankopen 12 %, niet aftrekbaar | 12 % | 0 % | 81/82/83 | 81/82/83, 85 |
| AEU21 | Intracommunautaire verwerving van goederen 21 % | 21 % | 100 % | 81/82/83, 86, 55, 59 | 81/82/83, 86, 84 |
| AEU12 | Intracommunautaire verwerving van goederen 12 % | 12 % | 100 % | 81/82/83, 86, 55, 59 | 81/82/83, 86, 84 |
| AEU6 | Intracommunautaire verwerving van goederen 6 % | 6 % | 100 % | 81/82/83, 86, 55, 59 | 81/82/83, 86, 84 |
| AEUS21 | Ontvangen intracommunautaire diensten 21 % | 21 % | 100 % | 81/82/83, 88, 55, 59 | 81/82/83, 88, 84 |
| ACC21 | Belgische medecontractant (verlegging) 21 % | 21 % | 100 % | 81/82/83, 87, 56, 59 | 81/82/83, 87, 85 |
| ACC12 | Belgische medecontractant (verlegging) 12 % | 12 % | 100 % | 81/82/83, 87, 56, 59 | 81/82/83, 87, 85 |
| ACC6 | Belgische medecontractant (verlegging) 6 % | 6 % | 100 % | 81/82/83, 87, 56, 59 | 81/82/83, 87, 85 |
| AWS21 | Diensten van buiten de EU 21 % (verlegging) | 21 % | 100 % | 81/82/83, 87, 56, 59 | 81/82/83, 87, 85 |
| AIM21 | Invoer, btw verlegd (ET 14000) 21 % | 21 % | 100 % | 81/82/83, 87, 57, 59 | 81/82/83, 87, 85 |
| R61 | Regularisatie ten gunste van de Staat | — | — | 61 | 61 |
| R62 | Regularisatie ten gunste van de aangever | — | — | 62 | 62 |

Rooster 81, 82 of 83 is de aard van de aankooprekening: handelsgoederen, diensten of investeringen
(paragraaf 4.5).

## Bijlage B. De samenvoegvelden

De velden die een sjabloon kan bevatten — in Letters getypt als samenvoegvelden (**Tools ▸ Mail Merge**).
De velden die eindigen op **Line**, **Address**, **Contact** en **Text** zijn gemaakt om zo te worden
afgedrukt: een label en zijn waarde, in de taal van het document, of helemaal niets als er geen waarde
is.

**Het document**

| Veld | Inhoud |
|---|---|
| Kind | De soort document, in zijn taal: *Devis*, *Factuur*, *Invoice*... |
| Number | Zijn nummer: *2026/0002* |
| Date | Zijn datum |
| Until | De geldigheid van een offerte, de leverdatum van een bestelling |
| UntilLine | *Geldig tot 15/10/2026* |
| DueDate | De vervaldag van een factuur |
| Reference, ReferenceLine | De referentie van de derde; met haar label |
| Text | De omschrijving |
| Communication | De gestructureerde mededeling van een factuur |
| Terms, TermsText | De betalingstermijn in dagen; zoals afgedrukt (*30 dagen*, *contant*) |
| FromDocument, FromLine | Het document waaruit het voortkomt; met zijn label |
| TotalNet, TotalVAT, Total | De totalen |
| VATDetail | De btw per tarief, en de wettelijke vermeldingen van verlegging en vrijstelling |
| FileName | De naam van het geschreven document |

**De vennootschap**

| Veld | Inhoud |
|---|---|
| CompanyName, CompanyLegal | De naam, de rechtsvorm |
| CompanyStreet, CompanyZip, CompanyCity, CompanyCountry, CompanyAddress | Het adres (CompanyAddress: zijn regels) |
| CompanyVAT, CompanyVATLine | Het btw-nummer; met zijn label |
| CompanyEmail, CompanyPhone, CompanyWeb, CompanyContact | De contactgegevens (CompanyContact: allemaal) |
| CompanyIBAN, CompanyBIC, CompanyBankLine | De bank; op één regel |
| CompanyRegister, CompanyLegalLine | Het register; de wettelijke regel — naam en vorm, adres, btw-nummer, register |

**De derde**

| Veld | Inhoud |
|---|---|
| PartyName, PartyCode | De naam van de klant of leverancier, zijn code |
| PartyStreet, PartyZip, PartyCity, PartyCountry, PartyAddress | Zijn adres |
| PartyVAT, PartyVATLine | Zijn btw-nummer; met zijn label |
| PartyEmail, PartyPhone | Zijn contactgegevens |

**De lijnen** — in een tabelrij, herhaald voor elke lijn

| Veld | Inhoud |
|---|---|
| LineNo | Het nummer van de lijn |
| LineText | Haar omschrijving |
| LineQty | De hoeveelheid |
| LinePrice | De eenheidsprijs zonder btw |
| LineVAT | Het btw-tarief |
| LineTotal | Het totaal zonder btw |
| LineTax | De btw |
| LineGross | Het totaal inclusief btw |

## Bijlage C. Het toetsenbord

| Toetsen | Waar | Wat ze doen |
|---|---|---|
| `Ctrl+N` | Overal | Een nieuw document van de getoonde pagina (een nieuwe fiche, een nieuwe rekening...) |
| `Ctrl+S` | Een document | Het opslaan |
| `Esc` | Een document | Het verlaten (Ledger vraagt het als er iets werd getypt) |
| `Ctrl+F` | Een lijst | Naar het zoekvak gaan |
| `Ctrl+O` | Overal | De boekhouding van een andere vennootschap openen |
| `Enter` | Een lijst | De gekozen rij openen |
| `Delete` | Een lijst | De gekozen rij verwijderen |
| `Space` | De rekening van een derde, de posten van een uittreksel | De gekozen lijn aan- of uitvinken |
| `Tab`, `Enter` | De lijnen van een document | De volgende cel; na de laatste een nieuwe lijn |
| `Shift+Tab` | De lijnen van een document | De vorige cel |
| `↑`, `↓` | De lijnen van een document | De lijn erboven, eronder |
| `F4`, `Alt+↓` | Een cel voor een derde, rekening of btw-code | De lijst van wat er kan worden getypt |
| `Ctrl+Delete` | De lijnen van een document | De lijn verwijderen |
| `←`, `→` | De btw-pagina | De vorige, de volgende periode |
