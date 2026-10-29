# IO-Link

# Test Specification

### Specification

Version V1.1.5

October 2025

Order No: 10.032

2025/10/30 12:04

### Test Specification © IO-Link – 1 – Version V1.1.5

## File name: IO-Link_Test-Spec_100032_V1.1.5_Oct-2025.docx

### This specification has been prepared by the IO-Link Test team and released by the IO-Link

### community as final version.

### Any comments, proposals, requests on this document are appreciated. Please use

### www.io-link-projects.com for your entries and provide name and email address.

### Login: IOL-Test-V113

### Password: Report

NOTICE:

The information contained in this document is subject to change without notice. The material in this document details a PNO specification in accordance with the license and notices set forth on this page. This document does not represent a commitment to implement any portion of a PNO specification in any company's products.

The attention of adopters is directed to the possibility that compliance with or adoption of PNO specifications may require use of an invention covered by patent rights. PNO shall not be responsible for identifying patents for which a license may be required by any PNO specification, or for conducting legal inquiries into the legal validity or scope of those patents that are brought to its attention. PNO specifications are prospective and advisory only. Prospective users are responsible for protecting themselves against liability for infringement of patents.

WHILE THE INFORMATION IN THIS DOCUMENT IS BELIEVED TO BE ACCURATE, PNO MAKES NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, WITH REGARD TO THIS DOCUMENT INCLUDING, BUT NOT LIMITED TO ANY WARRANTY OF TITLE OR OWNERSHIP, IMPLIED WARRANTY OF MERCHANTABILITY OR WARRANTY OF FITNESS FOR PARTICU- LAR PURPOSE OR USE.

In no event shall PNO be liable for errors contained herein or for indirect, incidental, special, consequential, reliance or cover damages, including loss of profits, revenue, data or use, incurred by any user or any third party. Compliance with PNO specifications does not absolve manufacturers, from the requirements of safety and regulatory agencies (TÜV, BIA, UL, CSA, etc.).

USE OF TRADEMARKS:

### ® is registered trade mark. The use is restricted for members of the IO-Link Community.

### More detailed terms for the use can be found in the IO-Link Community Rules on www.io-link.com.

PNO is the owner of several registered trademarks, such as PROFIBUS®, PROFINET®, omlox®, IO-Link®, MTP® and others. More detailed terms for the use can be found on the web page www.profibus.com. Please select buttons "Downloads / Presentations & logos". In some cases, PNO is the licensee of registered trademarks owned by third parties and which may be relevant in regard with products compliant to this document.

PNO shall always be the sole entity that may authorize developers, suppliers and sellers of hardware and software to use certification marks, trademarks or other special designations to indicate compliance with a PNO specification. Products developed using a PNO specification may claim compliance or conformance with a PNO specification only if the hardware and/or software satisfactorily meets the certification requirements set by PNO. Products that do not meet these requirements may claim only that the product was based on a PNO specification and must not claim compliance or conformance with a PNO specification.

COPYRIGHT

Copyright © 2024 PROFIBUS Nutzerorganisation e.V.

Any unauthorized use of this publication may violate Copyright Law, Trademark Law and other legal regulations. This document contains information which is protected by Copyright. No part of this work covered by Copyright herein may be reproduced or used in any form or by any means -graphic, electronic, or mechanical, including photocopying, recording, taping, or information storage and retrieval systems--without permission of the publisher.

Publisher:

### IO-Link Community

### c/o PROFIBUS Nutzerorganisation e.V.

### Ohiostrasse 8

### 76149 Karlsruhe

### Germany

### Phone: +49 721 / 98 61 97 0

### Fax: +49 721 / 98 61 97 11

### E-mail: info@io-link.com

### Web site: www.io-link.com

### – 3 –

LICENSE AGREEMENT

1. License

1. 1 Subject of this license agreement is this document issued by the Licensor, in electronic form. If applicable, also

software may be provided.

1. 2 The user of this document (Licensee) acquires the license solely from PROFIBUS Nutzerorganisation e.V., having

its principal place of business in Karlsruhe, Germany (hereinafter referred to as “Licensor”).

1. 3 This document is not an industrial standard acknowledged by any standardization body or otherwise and may be

further enhanced.

2. Rights and Duties of Licensee

2. 1 Licensor hereby grants to Licensee the right to use this document exclusively for developing and supporting

products compliant with this document. Licensee may copy this document for this purpose and for data backup purposes.

2. 2 Licensee shall not be entitled to modify, decompile, reverse engineer or extract any individual parts of this doc-

ument, unless this is permitted by mandatory Copyright Law. Furthermore, Licensee shall not be entitled to remove any alphanumeric identifiers, trademarks or copyright notices from this document and, insofar as Licensee is entitled to make copies of this document, Licensee shall copy them without alteration.

2. 3 Licensee is not entitled to copy and redistribute this document to any third party, except for “Have Made” pur-

poses. All copies must be obtained on an individual basis, directly from the website www.profibus.com or upon request from the Licensor.

2. 4. Licensee agrees to comply with all applicable export control, trade, and sanctions regulations as well as embargo

regulations of the European Union, the United States of America, and other relevant jurisdictions (“Export Control Regulations”). This applies in particular to the applicable Export Control Regulations relating to Russia and Belarus and with natural or legal persons associated with Russia and Belarus. Should Licensor become aware of any violation of Export Control Regulations with respect to the use of this document, PNO reserves the right to terminate this Agreement without notice and claim damages.

3. Liability of Licensor

3. 1 Licensor shall have no obligation to enhance the document and shall assume no liability in case the document or

future versions thereof shall not be approved as an industrial standard.

3. 2 Licensor’s liability for defects as to quality or title of this document, especially in relation to the correctness or absence

of defects or the absence of claims or third -party rights or in relation to completeness, usability and/or fitness for purpose are excluded, except for cases involving gross negligence, willful misconduct or fraudulent con-cealment of a defect.

3. 3 Any further liability is excluded unless required by law, e.g. in cases of personal injury or death, willful misconduct, gross

negligence, or in case of breach of fundamental contractual obligations. The damages in case of breach of fundamental contractual obligations is limited to the contract-typical, foreseeable damage if there is no willful mis-conduct or gross negligence.

4. Place of Jurisdiction and Applicable Law

4. 1 The sole place of jurisdiction shall be the principal place of business of Licensor.

4. 2 All relations arising out of the contract shall be governed by the substantive law of Germany, to the exclusion of the

United Nations Convention on Contracts for the International Sale of Goods (CISG).

Conventions:

In this specification the following key words (in bold text) will be used:

shall:

should: can: may: highly recommended:

indicates a mandatory requirement. Designers shall implement such mandatory requirements to ensure interoperability and to claim conformity with this specification. indicates flexibility of choice with a strongly preferred implementation. indicates flexibility of choice with no implied preference (possibility and capability). indicates a permission. indicates that a feature shall be implemented except for well -founded cases. Vendor shall document the deviation within the user manual and within the manufacturer declaration.

Test Specification © IO-Link – 3 – Version V1.1.5

### CONTENTS

1 Introduction...................................................................................................................23

1. 1 General.................................................................................................................23

1. 2 Patent declaration.................................................................................................23

2 Scope and objectives....................................................................................................25

3 Normative references....................................................................................................25

4 Terms, definitions, symbols, abbreviated terms and conventions...................................25

4. 1 Terms and definitions............................................................................................25

4. 2 Symbols and abbreviated terms............................................................................29

4. 3 Conventions..........................................................................................................31

4. 3.1 Test case template........................................................................................31

4. 3.2 Names of variables........................................................................................34

4. 3.3 Memory and transmission octet order............................................................34

4. 3.4 Behavioral descriptions..................................................................................35

5 Test strategy for SDCI (IO-Link TM) Master and Devices.................................................35

5. 1 Purpose of this specification.................................................................................35

5. 2 Structure of this document....................................................................................35

5. 3 Conformity classes...............................................................................................35

5. 3.1 Legacy Devices (V1.0)...................................................................................35

5. 3.2 Devices without ISDU....................................................................................36

5. 3.3 Devices with ISDU.........................................................................................36

5. 3.4 Legacy Master...............................................................................................36

5. 3.5 Master...........................................................................................................36

5. 4 Test of Devices.....................................................................................................36

5. 5 Test of Masters.....................................................................................................41

6 Physical Layer (PL) tests...............................................................................................45

6. 1 General.................................................................................................................45

6. 2 Static parameters of the Master interface..............................................................46

6. 2.1 DC supply current capability of Master Port L+..............................................46

6. 2.2 Power-On supply current capability of Master Port L+....................................47

6. 2.3 Load current at Master Port C/Q....................................................................48

6. 2.4 High-side residual voltage at Master Port C/Q...............................................49

6. 2.5 Low-side residual voltage at Master Port C/Q................................................50

6. 2.6 High-level input threshold voltage at Master Port C/Q....................................51

6. 2.7 Low-level input threshold voltage at Master Port C/Q.....................................52

6. 2.8 Input hysteresis voltage at Master Port C/Q...................................................53

6. 2.9 Permissible voltage range at Master Port C/Q...............................................54

6. 2.10 Electrical isolation of Master Port class B......................................................55

6. 3 Static parameters of the Device interface..............................................................56

6. 3.1 Power supply current consumption of the Device I.........................................56

6. 3.2 Power supply current consumption of the Device II........................................57

6. 3.3 Power-on behavior of the Device...................................................................58

6. 3.4 High-side residual voltage at Device C/Q.......................................................59

6. 3.5 Low-side residual voltage at Device C/Q........................................................60

6. 3.6 Pull-down or quiescent current at Device C/Q................................................61

6. 3.7 High-level input threshold voltage at Device C/Q...........................................62

Version V1.1.5 – 4 – Test Specification © IO-Link

6. 3.8 Low-level input threshold voltage at Device C/Q............................................63

6. 3.9 Input hysteresis voltage at Device C/Q..........................................................64

6. 3.10 Permissible voltage range at Device C/Q.......................................................65

6. 3.11 Electrical isolation of Device power supplies..................................................66

6. 4 Wake-Up generation of the Master interface.........................................................67

6. 4.1 Wake-Up current pulse high...........................................................................67

6. 4.2 Wake-Up pulse duration high.........................................................................68

6. 4.3 Wake-Up current pulse low............................................................................69

6. 4.4 Wake-Up pulse duration low..........................................................................70

6. 5 Wake-Up detection of the Device interface............................................................71

6. 5.1 Wake-Up pulse detection high.......................................................................71

6. 5.2 Wake-Up pulse detection low.........................................................................72

6. 5.3 Wake-Up receive enable delay (C/Q high).....................................................73

6. 5.4 Wake-Up receive enable delay (C/Q low).......................................................74

6. 5.5 SDCI readiness delay....................................................................................75

6. 5.6 Time to return to SIO after failed wake-up.....................................................76

6. 5.7 Time to Fallback after Master command........................................................77

6. 6 Dynamic parameters of the Master and Device interface.......................................78

6. 6.1 Bit eye-diagram with maximum load (Master).................................................78

6. 6.2 Bit eye-diagram with maximum load (Device).................................................79

6. 6.3 Bit eye-diagram with minimum load (Master)..................................................80

6. 6.4 Bit eye-diagram with minimum load (Device)..................................................81

6. 6.5 UART frame eye-diagram with maximum load (Master)..................................82

6. 6.6 UART frame eye-diagram with maximum load (Device)..................................83

6. 6.7 UART frame eye-diagram with minimum load (Master)...................................84

6. 6.8 UART frame eye-diagram with minimum load (Device)...................................85

6. 6.9 UART frame transmission delay of Master (Ports)..........................................86

6. 6.10 UART frame transmission delay of Device.....................................................87

6. 6.11 Response time of Device...............................................................................88

6. 6.12 Device response without transmission errors.................................................89

6. 7 Test report template for PL tests...........................................................................90

7 Device protocol test cases.............................................................................................92

7. 1 General.................................................................................................................92

7. 2 STARTUP.............................................................................................................92

7. 2.1 STARTUP cycle time.....................................................................................92

7. 2.2 From STARTUP to OPERATE........................................................................93

7. 2.3 Master start-up with overwrite of the RID (compatible)...................................94

7. 2.4 Illegal STARTUP to OPERATE......................................................................95

7. 2.5 From OPERATE to STARTUP via Master command......................................96

7. 2.6 From OPERATE to STARTUP via M-sequence TYPE_0................................97

7. 3 PREOPERATE......................................................................................................98

7. 3.1 From STARTUP to PREOPERATE Read.......................................................98

7. 3.2 From STARTUP to PREOPERATE Write.......................................................99

7. 3.3 From STARTUP to PREOPERATE short message.......................................100

7. 3.4 From PREOPERATE to STARTUP via simulated reset................................101

7. 3.5 From PREOPERATE to STARTUP with M-sequence fault............................102

7. 3.6 Minimum Recovery Time in PREOPERATE Mode........................................103

7. 4 OPERATE...........................................................................................................104

7. 4.1 From PREOPERATE to OPERATE Read.....................................................104

Test Specification © IO-Link – 5 – Version V1.1.5

7. 4.2 From PREOPERATE to OPERATE Write.....................................................105

7. 4.3 From PREOPERATE to OPERATE negative Write.......................................106

7. 4.4 From OPERATE to STARTUP via simulated reset.......................................107

7. 4.5 Actuator behavior at PDOut invalid..............................................................108

7. 4.6 Actuator behavior at communication loss.....................................................109

7. 4.7 PD Status for output Process Data only.......................................................110

7. 5 ISDU (Indexed Service Data Unit).......................................................................111

7. 5.1 Prearrangement measures and configuration...............................................111

7. 5.2 Availability of ISDU via M-sequence Capability............................................112

7. 5.3 "Idle/Busy" check.........................................................................................113

7. 5.4 Read 8-bit Index..........................................................................................114

7. 5.5 Read 8-bit Index with ExtLength..................................................................115

7. 5.6 Write 8-bit Index..........................................................................................116

7. 5.7 Read 8-bit Index reserved...........................................................................117

7. 5.8 Read 8-bit Index with unavailable Subindex.................................................118

7. 5.9 Read 16-bit Index........................................................................................119

7. 5.10 Write 16-bit Index........................................................................................120

7. 5.11 Read 16-bit Index reserved..........................................................................121

7. 5.12 Read 16-bit Index with unavailable Subindex...............................................122

7. 5.13 Write 8-bit Index with data length overrun....................................................123

7. 5.14 Write 8-bit Index with data length underrun..................................................124

7. 5.15 Read 8-bit Index with incorrect Checksum value..........................................125

7. 5.16 Write 8-bit Index on read only Index............................................................126

7. 5.17 Read 8-bit Index with aborted request.........................................................127

7. 5.18 Read 8-bit Index with aborted response.......................................................128

7. 5.19 Master retries when ISDU transfer failed.....................................................129

7. 6 Events................................................................................................................130

7. 6.1 General.......................................................................................................130

7. 6.2 Single Event while in OPERATE state.........................................................131

7. 6.3 Single Event while in PREOPERATE state...................................................133

7. 6.4 Event clearance in OPERATE state.............................................................135

7. 6.5 Event handling while communication interruption.........................................136

7. 6.6 Event handling while power supply interruption............................................137

7. 6.7 Event appears/disappears...........................................................................138

7. 6.8 Multi Event handling....................................................................................140

7. 6.9 Short time Events........................................................................................142

7. 6.10 Interconnection active Event/Device Status.................................................144

7. 7 Data Storage (DS)..............................................................................................145

7. 7.1 General.......................................................................................................145

7. 7.2 Upload without DS_UPLOAD_FLAG notification..........................................146

7. 7.3 Upload via ParamDownloadStore................................................................147

7. 7.4 Upload via ParamDownloadStore without write calls....................................148

7. 7.5 Upload via local parameter modification......................................................150

7. 7.6 Call ParamBreak in different states of Upload..............................................152

7. 7.7 Check structure of Data Storage Index List..................................................154

7. 7.8 Download after modification of parameters..................................................155

7. 7.9 Download into the Device after reset...........................................................156

7. 7.10 Call ParamBreak in different states of Download.........................................157

7. 7.11 Parameter Manager – Single Parameter and DownloadStore.......................158

Version V1.1.5 – 6 – Test Specification © IO-Link

7. 7.12 Clear DS_UPLOAD_FLAG after DS_Up/DownloadEnd................................159

7. 7.13 Storage of DS_UPLOAD_FLAG in non volatile memory...............................160

7. 8 Operation with a legacy Master ("Master 1.0")....................................................161

7. 8.1 General.......................................................................................................161

7. 8.2 Conformity classes......................................................................................161

7. 8.3 From STARTUP to OPERATE (V1.0)...........................................................162

7. 8.4 From STARTUP to OPERATE – interleave (V1.0)........................................163

7. 8.5 Events – PDInvalid / PDValid (V1.0)............................................................164

7. 9 Direct Parameter page 1.....................................................................................165

7. 9.1 MasterCycleTime.........................................................................................165

7. 9.2 MinCycleTime..............................................................................................166

7. 9.3 M-sequenceCapability.................................................................................167

7. 9.4 RevisionID...................................................................................................168

7. 9.5 ProcessDataIn.............................................................................................169

7. 9.6 ProcessDataOut..........................................................................................170

7. 9.7 VendorID.....................................................................................................171

7. 9.8 DeviceID......................................................................................................172

7. 9.9 FunctionID...................................................................................................173

7. 9.10 Reserved parameter – Read/Write...............................................................174

7. 9.11 Validity of MinCycleTime.............................................................................175

7. 10 Predefined Device parameters............................................................................176

7. 10.1 General rules...............................................................................................176

7. 10.2 System command – reserved commands.....................................................177

7. 10.3 Data Storage Index – entire parameter structure.........................................178

7. 10.4 Data Storage Index – record items...............................................................179

7. 10.5 Data Storage Index – termination marker.....................................................180

7. 10.6 Device Access Locks – valid........................................................................181

7. 10.7 Device Access Locks – invalid.....................................................................182

7. 10.8 Profile Characteristic...................................................................................183

7. 10.9 PD Input Descriptor.....................................................................................184

7. 10.10 PD Output Descriptor...................................................................................185

7. 10.11 Vendor Name..............................................................................................186

7. 10.12 Vendor Text.................................................................................................187

7. 10.13 Product Name..............................................................................................188

7. 10.14 Product ID...................................................................................................189

7. 10.15 Product Text................................................................................................190

7. 10.16 Serial Number.............................................................................................191

7. 10.17 Hardware Revision......................................................................................192

7. 10.18 Firmware Revision.......................................................................................193

7. 10.19 Application Specific Tag – valid...................................................................194

7. 10.20 Application Specific Tag – invalid................................................................195

7. 10.21 Error Count..................................................................................................196

7. 10.22 Device Status..............................................................................................197

7. 10.23 Detailed Device Status – complete object....................................................198

7. 10.24 Detailed Device Status – Event active.........................................................199

7. 10.25 Process Data Input......................................................................................200

7. 10.26 Process Data Output...................................................................................201

7. 10.27 Offset Time – valid......................................................................................202

7. 10.28 Profile Parameter – Read access.................................................................203

Test Specification © IO-Link – 7 – Version V1.1.5

7. 10.29 Profile Parameter – Write access.................................................................204

7. 10.30 Write access – Read only parameter...........................................................205

7. 10.31 Write access – Length too short...................................................................206

7. 10.32 Write access – Length too long....................................................................207

7. 11 Block parameter..................................................................................................207

7. 11.1 General.......................................................................................................207

7. 11.2 Block parameter – Download.......................................................................208

7. 11.3 Block parameter – Break by command.........................................................209

7. 11.4 Block parameter – Break by reset................................................................210

7. 11.5 Block parameter – Illegal parameter write....................................................211

7. 11.6 Block parameter – Break by double download..............................................212

7. 11.7 Block parameter – local locking...................................................................213

7. 11.8 ParameterManager – Unexpected commands in Idle...................................214

7. 11.9 ParameterManager – Write request during Upload.......................................215

7. 11.10 ParameterManager – Read requests during Download................................216

7. 11.11 ParameterManager – Unexpected commands during Upload.......................217

7. 11.12 ParameterManager – Unexpected switches Upload/Download.....................218

7. 11.13 ParameterManager – Upload interrupted by reset........................................219

7. 11.14 ParameterManager – UploadEnd during Download......................................220

7. 12 Test report summary of the Device protocol tests................................................220

8 IODD-related tests.......................................................................................................221

8. 1 Overview............................................................................................................221

8. 2 Schema test via an IODD checker tool and IODD interpreter tests......................221

8. 2.1 IODD Checker.............................................................................................221

8. 2.2 IODD interpreter tool...................................................................................221

8. 3 Parameter verification test..................................................................................222

8. 3.1 IODD identification......................................................................................222

8. 3.2 IODD communication parameter verification................................................223

8. 3.3 IODD parameter read verification................................................................224

8. 3.4 IODD parameter write verification................................................................225

8. 3.5 IODD parameter Index/Subindex consistency..............................................227

8. 4 Functional system tests (IODD indication)...........................................................229

8. 4.1 General.......................................................................................................229

8. 4.2 IODD – functional verification of "Parameter (write) access lock".................230

8. 4.3 IODD – functional verification of "Device reset"...........................................231

8. 4.4 IODD – functional verification of "Application reset".....................................233

8. 4.5 IODD – functional verification of "Restore factory settings"..........................235

8. 4.6 IODD – functional verification of "Back-to-box"............................................237

8. 4.7 IODD – Write alternative valid DeviceID......................................................239

8. 4.8 IODD – Write alternative invalid DeviceID....................................................240

8. 5 Test report summary of the IODD based Device tests.........................................240

9 Master protocol tests...................................................................................................241

9. 1 General...............................................................................................................241

9. 2 Timings...............................................................................................................241

9. 2.1 Delay times after WURQ and Master messages (TDMT)..............................241

9. 2.2 Delay time between three WURQs (TDWU).................................................243

9. 2.3 Number of WURQs......................................................................................244

9. 2.4 Delay time between WURQ retry sequences (TSD).....................................245

9. 2.5 Delay time between two Master messages at STARTUP (TINITCYC)..........246

Version V1.1.5 – 8 – Test Specification © IO-Link

9. 2.6 Delay time of Master messages with 2 octets OD in PREOP (TINITCYC).....247

9. 2.7 Delay time of Master messages with 8 octets OD in PREOP (TINITCYC).....248

9. 2.8 Delay time of Master messages with 32 octets OD in PREOP

(TINITCYC).................................................................................................249

9. 2.9 Adjustment of the MasterCycleTime.............................................................250

9. 2.10 Written MasterCycleTime corresponds to real cycle time.............................251

9. 2.11 Master tolerates different Device response times.........................................252

9. 2.12 Master tolerates different UART frame delay times (T2)...............................253

9. 2.13 Master sends UART frames within tolerated times (T1)................................254

9. 3 Process Data (PD)..............................................................................................255

9. 3.1 TYPE_2_1 for 8 bit PD input........................................................................255

9. 3.2 TYPE_2_2 for 16 bit PD input......................................................................256

9. 3.3 TYPE_2_3 for 8 bit PD output......................................................................257

9. 3.4 TYPE_2_4 for 16 bit PD output....................................................................258

9. 3.5 TYPE_2_5 for 8/8 bit PD in/output...............................................................259

9. 3.6 TYPE_2_V for 16/16 bit PD in/output and 1 octet OD...................................260

9. 3.7 TYPE_1_1 for 32 octets PD in and 2 octets OD...........................................262

9. 3.8 TYPE_2_V for 0/32 octets PD in/out and variable octets OD........................264

9. 3.9 TYPE_2_V for 32/0 octets PD in/out and variable octets OD........................265

9. 3.10 Master reads mirrored PD in/out from Device..............................................266

9. 3.11 Master propagates "PD invalid" indication in a correct manner.....................267

9. 3.12 Master propagates "PD valid" indication in a correct manner.......................268

9. 4 On-request Data (OD).........................................................................................269

9. 4.1 TYPE_2_V for different PD in/out and 1 octet OD........................................269

9. 4.2 TYPE_2_V for different PD in/out and 2 octets OD......................................271

9. 4.3 TYPE_2_V for different PD in/out and 8 octets OD......................................273

9. 4.4 TYPE_2_V for different PD in/out and 32 octets OD....................................274

9. 4.5 Master sends Idle after an accomplished ISDU service................................276

9. 5 STARTUP...........................................................................................................277

9. 5.1 Master reads communication parameters (Direct Parameter).......................277

9. 5.2 Master adjusts to protocol V1.1 (Direct Parameter)......................................278

9. 5.3 Master adjusts to protocol V1.0 (Direct Parameter)......................................279

9. 5.4 Master start-up with non configured VID and DID........................................280

9. 5.5 Master start-up with configured VID and DID...............................................281

9. 5.6 Master start-up with overwrite of the DID (compatible).................................282

9. 5.7 Master start-up with overwrite of the DID (incompatible)..............................283

9. 5.8 Master start-up with overwrite of the RID (incompatible)..............................284

9. 5.9 Master start-up with non configured VID and DID (V1.0)..............................286

9. 5.10 Master start-up with configured VID and DID (Device V1.0).........................287

9. 5.11 Master start-up with incorrect DID (Device V1.0).........................................288

9. 5.12 Master start-up with overwrite of the RID (compatible).................................289

9. 6 PREOPERATE....................................................................................................290

9. 6.1 Master in PREOPERATE uses TYPE_0 to read Index 24 (1 OD).................290

9. 6.2 Master in PREOPERATE uses TYPE_1_2 to read Index 24 (2OD)...............291

9. 6.3 Master in PREOPERATE uses TYPE_1_V to read Index 24 (8 OD).............292

9. 6.4 Master in PREOPERATE uses TYPE_1_V to read Index 24 (32 OD)...........293

9. 6.5 Master in PREOPERATE uses TYPE_0 to write Index 24 (1 OD).................294

9. 6.6 Master in PREOPERATE uses TYPE_1_2 to write Index 24 (2 OD).............295

9. 6.7 Master in PREOPERATE uses TYPE_1_V to write Index 24 (8 OD).............296

Test Specification © IO-Link – 9 – Version V1.1.5

9. 6.8 Master in PREOPERATE uses TYPE_1_V to write Index 24 (32 OD)...........297

9. 7 OPERATE...........................................................................................................298

9. 7.1 Master in OPERATE uses TYPE_1_V for zero bit PD in/out (8 OD).............298

9. 7.2 Master in OPERATE uses TYPE_1_V for zero bit PD in/out (32 OD)...........299

9. 7.3 Master in OPERATE uses TYPE_0 to read Index 24 (1 OD)........................300

9. 7.4 Master in OPERATE uses TYPE_0 to write Index 24 (1 OD)........................301

9. 7.5 Master in OPERATE uses TYPE_1_2 to write Index 24 (2 OD)....................302

9. 8 Fallback..............................................................................................................303

9. 8.1 Fallback from PREOPERATE......................................................................303

9. 8.2 Fallback from PREOPERATE fails...............................................................304

9. 8.3 Fallback from OPERATE.............................................................................305

9. 8.4 Fallback from OPERATE fails......................................................................306

9. 9 Retry...................................................................................................................307

9. 9.1 Retries after 2 reply messages with incorrect Checksum (STARTUP)..........307

9. 9.2 Retries after 2 reply messages with incorrect Checksum (PREOP)..............308

9. 9.3 Retries after 2 reply messages with incorrect Checksum (OPERATE)..........309

9. 9.4 Retries after 3 reply messages with incorrect Checksum (STARTUP)..........310

9. 9.5 Retries after 3 reply messages with incorrect Checksum (PREOP)..............311

9. 9.6 Retries after 3 reply messages with incorrect Checksum (OPERATE)..........312

9. 9.7 Retries after 2 missing replies and finally correct Checksum (STARTUP)....313

9. 9.8 Retries after 2 missing replies and finally correct Checksum (PREOP)........314

9. 9.9 Retries after 2 missing replies and finally correct Checksum

(OPERATE).................................................................................................315

9. 9.10 Retries after 3 missing replies ending with restart/stop (STARTUP).............316

9. 9.11 Retries after 3 missing replies ending with restart/stop (PREOP).................317

9. 9.12 Retries after 3 missing replies ending with restart/stop (OPERATE).............318

9. 9.13 Maximum WURQs and no final success.......................................................319

9. 10 ISDU (Indexed Service Data Unit) – Application ErrorTypes................................320

9. 10.1 ISDU Write rejected with ErrorType.............................................................320

9. 10.2 ISDU Write to unsupported Index rejected with ErrorType...........................321

9. 10.3 ISDU Write to unsupported Subindex rejected with ErrorType......................322

9. 10.4 ISDU Write to temporarily unavailable Index rejected with ErrorType...........323

9. 10.5 ISDU Write to temporarily unavailable Index due to local control.................324

9. 10.6 ISDU Write to temporarily unavailable Index due to Device control..............325

9. 10.7 ISDU Write to read-only Index denied..........................................................326

9. 10.8 ISDU Write with invalid Length....................................................................327

9. 10.9 ISDU Write with parameter value out of range.............................................328

9. 10.10 ISDU Write with parameter value above limit...............................................329

9. 10.11 ISDU Write with parameter value below limit................................................330

9. 10.12 ISDU Write with invalid parameter set..........................................................331

9. 10.13 ISDU Write while Device application fault....................................................332

9. 10.14 ISDU Write to reserved Indices....................................................................333

9. 10.15 ISDU Write to reserved Indices and no ISDU...............................................334

9. 11 ISDU (Indexed Service Data Unit) – Derived ErrorTypes.....................................335

9. 11.1 ISDU Write response without busy indication...............................................335

9. 11.2 ISDU Write response with timeout after busy indication...............................336

9. 11.3 ISDU Write response with illegal service code.............................................337

9. 11.4 ISDU Write response with wrong checksum (CHKPDU)...............................338

9. 11.5 ISDU Write response with reserved data length...........................................339

Version V1.1.5 – 10 – Test Specification © IO-Link

9. 12 ISDU (Indexed Service Data Unit) – Limit checks................................................340

9. 12.1 ISDU Read response without data...............................................................340

9. 12.2 ISDU Write with minimum data length (zero octets).....................................341

9. 12.3 ISDU Write with maximum service length (238 octets).................................342

9. 12.4 ISDU Read with maximum service length (238 octets).................................343

9. 12.5 ISDU Write to 8-bit Index and no Subindex..................................................344

9. 12.6 ISDU Write to 8-bit Index and 8-bit Subindex...............................................345

9. 12.7 ISDU Write to 16-bit Index and 8-bit Subindex.............................................346

9. 12.8 ISDU Write response without busy bit..........................................................347

9. 12.9 ISDU Write response with busy bit...............................................................348

9. 12.10 ISDU Write with maximum service Length (15 octets)..................................349

9. 12.11 ISDU Write with minimum service Extended Length (17)..............................350

9. 13 Events................................................................................................................351

9. 13.1 General.......................................................................................................351

9. 13.2 Master receives Events without details (notification)....................................351

9. 13.3 Master receives event with details (single event).........................................352

9. 13.4 Master receives event with details (double event)........................................353

9. 13.5 Events of type notification............................................................................355

9. 13.6 Master receives Event with details (six Events)...........................................356

9. 13.7 Master receives Event while in ISDU Write transfer (stopover).....................358

9. 13.8 Master receives Event while in ISDU Read transfer (stopover)....................359

9. 13.9 Master receives Event details while in ISDU Write transfer (stopover).........360

9. 13.10 Master receives Event details while in ISDU Read transfer (stopover).........361

9. 13.11 Behavior and Port Event at Device lost (COMLOST)....................................362

9. 13.12 Event in PREOPERATE...............................................................................364

9. 13.13 Event in PORT_DIAG..................................................................................365

9. 13.14 Invalid cycle time requested........................................................................366

9. 14 Data Storage (DS)..............................................................................................367

9. 14.1 General.......................................................................................................367

9. 14.2 Delete stored DS object after reconfiguration...............................................367

9. 14.3 Data Storage size limits (quantities)............................................................369

9. 14.4 Write consistent DS data object from parameter server...............................370

9. 14.5 Write inconsistent DS data object from parameter server.............................371

9. 14.6 Write DS data object to improper Port configuration.....................................372

9. 14.7 Upload request Event in OPERATE (Backup + Restore)..............................373

9. 14.8 Upload request Event in OPERATE (Restore)..............................................374

9. 14.9 Upload request Event in OPERATE (Backup + Restore inactive).................375

9. 14.10 Start-up with empty DS (DS deactivated).....................................................376

9. 14.11 Start-up with empty DS (DS activated – DS Upload)....................................377

9. 14.12 Start-up with stored DS (Device replacement – DS Download)....................378

9. 14.13 Start-up with stored DS (Device replacement – no DS Download)................379

9. 14.14 Start-up with stored DS (Device replacement – DS Upload).........................380

9. 14.15 Start-up with stored DS (Device replacement – no DS Upload)....................381

9. 14.16 Corrupted DS Index_List..............................................................................382

9. 14.17 DS Download fault.......................................................................................384

9. 14.18 DS Upload fault...........................................................................................386

9. 14.19 Incompatible Device and DS........................................................................388

9. 14.20 Master power OFF/ON (non-volatile DS data)..............................................390

9. 14.21 DS Upload trial with locked Device Data Storage.........................................391

Test Specification © IO-Link – 11 – Version V1.1.5

9. 15 Legacy Device ("V1.0").......................................................................................392

9. 15.1 General.......................................................................................................392

9. 15.2 Master detects legacy Device and establishes connection...........................392

9. 15.3 Master detects legacy Device and establishes interleave mode...................393

9. 15.4 Master receives an Event without details (Warning).....................................394

9. 15.5 ISDU Write interrupted by an Event leads to a Write error...........................395

9. 15.6 Master transforms PD_invalid Event into appropriate propagation...............396

9. 16 Test report template............................................................................................397

10 Environmental tests.....................................................................................................398

10. 1 General...............................................................................................................398

10. 2 Product specific standards..................................................................................398

10. 3 EMC tests...........................................................................................................398

10. 4 Test report templates..........................................................................................398

10. 4.1 Overview.....................................................................................................398

10. 4.2 ESD.............................................................................................................399

10. 4.3 HF...............................................................................................................401

10. 4.4 Burst...........................................................................................................403

10. 4.5 Conducted RF.............................................................................................405

10. 4.6 Explanation of template terms.....................................................................406

(normative) Test configurations and test tools.............................................................407

A.1 Test configurations.............................................................................................407 A.1.1 Overview.....................................................................................................407 A.1.2 Measurement circuits for electrical isolation.................................................407 A.1.3 Measurement circuits for the physical layer tests.........................................408 A.1.4 Protocol recording via a Line-Monitor..........................................................413 A.1.5 Requirements for a Reference-Master and a Reference-Device...................413 A.1.6 Diagrams for evaluations.............................................................................414 A.2 Device-Tester-System.........................................................................................415 A.2.1 Overview.....................................................................................................415 A.2.2 Test principle and requirements...................................................................415 A.3 Master-Tester-System and approach to Master testing........................................416 A.3.1 Master-Tester using common communication interfaces..............................416 A.3.2 System requirements for Master-Tester.......................................................417 A.3.3 SMI Test Communication Server (STCS).....................................................418 A.4 Components of Master test cases.......................................................................419 A.4.1 Overview.....................................................................................................419 A.4.2 EUT preconditions (Master Port)..................................................................420 A.4.3 MTU preconditions.......................................................................................420 A.4.4 TestMacros (TM) of the Master-Tester-Program..........................................421 A.4.5 SMI Event Handling.....................................................................................421 A.4.6 SMI ArgBlock parameter sets (ABPS)..........................................................421 A.4.7 MTU instructions..........................................................................................423 A.4.8 MTU data sets.............................................................................................426 (normative) Supplement to the legacy specification V1.0.............................................428

B.1 General...............................................................................................................428 B.2 Legacy-Master power-on driver capability...........................................................428 B.3 Legacy-Device power-on current consumption....................................................428 B.4 ISDU request and response abort.......................................................................428 B.5 "Device 1.1" connected to a "Master 1.0"............................................................428

Version V1.1.5 – 12 – Test Specification © IO-Link

B.6 Maximum MasterCycleTime................................................................................428 B.7 Maximum MinCycleTime.....................................................................................428 B.8 Write access to reserved system commands.......................................................428 B.9 Time-out for Write access to system commands..................................................428 B.10 Text string length for Application Specific Tag.....................................................428 B.11 Write access with invalid length..........................................................................429 B.12 IODD "reset to factory settings" verification.........................................................429 B.13 Fallback in PREOPERATE..................................................................................429 (normative) Listing of test cases..................................................................................430

C.1 Listing of test cases sorted by IDs......................................................................430 (informative) Information on conformity testing of SDCI...............................................438

Bibliography........................................................................................................................439

Figure 1 – Structure of the test case name............................................................................32

Figure 2 – Memory and transmission octet order...................................................................35

Figure 3 – Step 1 of the Device test sequence (PL)..............................................................37

Figure 4 – Step 2 of the Device test sequence (IODD)..........................................................38

Figure 5 – Step 3 of the Device test sequence (protocol)......................................................39

Figure 6 – Step 4 of the Device test sequence (environment)................................................41

Figure 7 – Step 1 of the Master test sequence (PL)..............................................................42

Figure 8 – Step 2 of the Master test sequence (PL)..............................................................43

Figure 9 – Step 3 of the Master test sequence (Interpreter)..................................................44

Figure 10 – Step 4 of the Master test sequence (environment)..............................................45

Figure 11 – Relationship of an Error and the Event Flag.....................................................139

Figure 12 – Correlation of two Errors and the Event Flag....................................................141

Figure 13 – Timings of a short time Error and the Event flag...............................................143

Figure 14 – Proposed template for ESD tests.....................................................................400

Figure 15 – Proposed template for HF tests........................................................................401

Figure 16 – Proposed template for Burst tests....................................................................403

Figure 17 – Proposed template for conducted RF tests.......................................................405

Figure A.1 – Measurement of Master Port class B isolation.................................................407

Figure A.2 – Measurement of Device isolation....................................................................407

Figure A.3 – Measurement circuit diagram for static parameters.........................................408

Figure A.4 – Measurement circuit diagram for power supply behavior.................................408

Figure A.5 – Measurement circuit diagram for dynamic parameters....................................408

Figure A.6 – Measurement circuit diagram for input thresholds...........................................409

Figure A.7 – Measurement circuit diagram for Wake-up requests (high)..............................409

Figure A.8 – Measurement circuit diagram for Wake-up requests (low)...............................410

Figure A.9 – Measurement of return time delay to SIO mode..............................................410

Figure A.10 – Measurement circuit diagram for input thresholds.........................................410

Figure A.11 – Measurement circuit diagram for Wake-up request timings............................411

Figure A.12 – Measurement circuit diagram for timing tREN (C/Q high)..............................411

Figure A.13 – Measurement circuit diagram for timing tREN (C/Q low)................................412

Figure A.14 – Measurement of start-up and readiness timings............................................412

Test Specification © IO-Link – 13 – Version V1.1.5

Figure A.15 – Eye diagram measurement using a line simulation........................................413

Figure A.16 – Message recording via Line-Monitor..............................................................413

Figure A.17 – Evaluation of Wake-up pulse duration high...................................................414

Figure A.18 – Evaluation of Wake-up pulse duration low.....................................................415

Figure A.19 – Principle of a Device-Tester-System.............................................................416

Figure A.20 – Principle of a Master-Tester-System using SMI.............................................417

Figure A.21 – Components of Master test cases.................................................................420

Table 1 – Test case template................................................................................................31

Table 2 – Test case categories.............................................................................................33

Table 3 – Test case types.....................................................................................................33

Table 4 – Set of test cases for Devices without ISDU support...............................................39

Table 5 – Set of test cases for Devices with ISDU support....................................................40

Table 6 – Set of test cases for Masters (V1.1)......................................................................43

Table 7 – DC supply current capability of Master Port L+......................................................46

Table 8 – Power-On supply current capability of Master Port L+...........................................47

Table 9 – Load current at Master Port C/Q............................................................................48

Table 10 – High-side residual voltage at Master Port C/Q.....................................................49

Table 11 – Low-side residual voltage at Master Port C/Q......................................................50

Table 12 – High-level input threshold voltage at Master Port C/Q..........................................51

Table 13 – Low-level input threshold voltage at Master Port C/Q..........................................52

Table 14 – Input hysteresis voltage at Master Port C/Q.........................................................53

Table 15 – Permissible voltage range at Master Port C/Q.....................................................54

Table 16 – Electrical isolation of Master Port class B............................................................55

Table 17 – Power supply current consumption of the Device I...............................................56

Table 18 – Power supply current consumption of the Device II..............................................57

Table 19 – Power-on behavior of the Device.........................................................................58

Table 20 – High-side residual voltage at Device C/Q............................................................59

Table 21 – Low-side residual voltage at Device C/Q.............................................................60

Table 22 – Pull-down or quiescent current at Device C/Q......................................................61

Table 23 – High-level input threshold voltage at Device C/Q.................................................62

Table 24 – Low-level input threshold voltage at Device C/Q..................................................63

Table 25 – Input hysteresis voltage at Device C/Q................................................................64

Table 26 – Permissible voltage range at Device C/...............................................................65

Table 27 – Electrical isolation of Device power supplies.......................................................66

Table 28 – Wake-Up current pulse high................................................................................67

Table 29 – Wake-Up pulse duration high...............................................................................68

Table 30 – Wake-Up current pulse low..................................................................................69

Table 31 – Wake-Up pulse duration low................................................................................70

Table 32 – Wake-Up pulse detection high.............................................................................71

Table 33 – Wake-Up pulse detection low..............................................................................72

Table 34 – Wake-Up receive enable delay (C/Q high)...........................................................73

Table 35 – Wake-Up receive enable delay (C/Q low)............................................................74

Version V1.1.5 – 14 – Test Specification © IO-Link

Table 36 – SDCI readiness delay..........................................................................................75

Table 37 – Time to return to SIO after failed wake-up...........................................................76

Table 38 – Time to Fallback after Master command..............................................................77

Table 39 – Bit eye-diagram with maximum load (Master)......................................................78

Table 40 – Bit eye-diagram with maximum load (Device)......................................................79

Table 41 – Bit eye-diagram with minimum load (Master).......................................................80

Table 42 – Bit eye-diagram with minimum load (Device).......................................................81

Table 43 – UART frame eye-diagram with maximum load (Master).......................................82

Table 44 – UART frame eye-diagram with maximum load (Device).......................................83

Table 45 – UART frame eye-diagram with minimum load (Master)........................................84

Table 46 – UART frame eye-diagram with minimum load (Device)........................................85

Table 47 – UART frame transmission delay of Master (Ports)...............................................86

Table 48 – UART frame transmission delay of Device...........................................................87

Table 49 – Response time of Device.....................................................................................88

Table 50 – Device response without transmission errors.......................................................89

Table 51 – Template for the test report of the Physical Layer tests.......................................90

Table 52 – STARTUP cycle time...........................................................................................92

Table 53 – From STARTUP to OPERATE.............................................................................93

Table 54 – Master start-up with overwrite of the RID (compatible).........................................94

Table 55 – Illegal STARTUP to OPERATE............................................................................95

Table 56 – From OPERATE to STARTUP via Master command............................................96

Table 57 – From OPERATE to STARTUP via M-sequence TYPE_0......................................97

Table 58 – From STARTUP to PREOPERATE Read.............................................................98

Table 59 – From STARTUP to PREOPERATE Write.............................................................99

Table 60 – From STARTUP to PREOPERATE short message.............................................100

Table 61 – From PREOPERATE to STARTUP via simulated reset......................................101

Table 62 – From PREOPERATE to STARTUP with M-sequence fault.................................102

Table 63 – From Minimum Recovery Time in PREOPERATE Mode.....................................103

Table 64 – From PREOPERATE to OPERATE Read...........................................................104

Table 65 – From PREOPERATE to OPERATE Write...........................................................105

Table 66 – From PREOPERATE to OPERATE negative Write.............................................106

Table 67 – From OPERATE to STARTUP via simulated reset.............................................107

Table 68 – Actuator behavior at PDOut invalid....................................................................108

Table 69 – Actuator behavior at communication loss...........................................................109

Table 70 – PD Status for output Process Data only.............................................................110

Table 71 – Availability of ISDU via M-sequence Capability..................................................112

Table 72 – "Idle/Busy" check..............................................................................................113

Table 73 – Read 8-bit Index................................................................................................114

Table 74 – Read 8-bit Index with ExtLength........................................................................115

Table 75 – Write 8-bit Index................................................................................................116

Table 76 – Read 8-bit Index reserved.................................................................................117

Table 77 – Read 8-bit Index with unavailable Subindex......................................................118

Table 78 – Read 16-bit Index..............................................................................................119

Test Specification © IO-Link – 15 – Version V1.1.5

Table 79 – Write 16-bit Index..............................................................................................120

Table 80 – Read 16-bit Index reserved...............................................................................121

Table 81 – Read 16-bit Index with unavailable Subindex.....................................................122

Table 82 – Write 8-bit Index with data length overrun..........................................................123

Table 83 – Write 8-bit Index with with data length underrun................................................124

Table 84 – Read 8-bit Index with incorrect Checksum value................................................125

Table 85 – Write 8-bit Index on read only Index..................................................................126

Table 86 – Read 8-bit Index with aborted request...............................................................127

Table 87 – Read 8-bit Index with aborted response.............................................................128

Table 88 – Master retries when ISDU transfer failed...........................................................129

Table 89 – Single Event while in OPERATE state...............................................................131

Table 90 – Single Event while in PREOPERATE.................................................................133

Table 91 – Event clearance in OPERATE state...................................................................135

Table 92 – Event handling while communication interruption...............................................136

Table 93 – Event handling while power supply interruption.................................................137

Table 94 – Event appears/disappears.................................................................................138

Table 95 – Multi Event handling..........................................................................................140

Table 96 – Short time Events..............................................................................................142

Table 97 – Interconnection active Event/Device Status/Detailed Device Status..................144

Table 98 – Upload without DS_UPLOAD_FLAG notification................................................146

Table 99 – Upload via ParamDownloadStore......................................................................147

Table 100 – Upload via ParamDownloadStore without write calls........................................148

Table 101 – Upload via local parameter modification..........................................................150

Table 102 – Call ParamBreak in different states of Upload.................................................152

Table 103 – Check structure of Data Storage Index List......................................................154

Table 104 – Download after modification of parameters......................................................155

Table 105 – Download into the Device after reset...............................................................156

Table 106 – Call ParamBreak in different states of Download.............................................157

Table 107 – Parameter Manager – Single Parameter and DownloadStore...........................158

Table 108 – Clear DS_UPLOAD_FLAG after DS_Up/DownloadEnd....................................159

Table 109 – Storage of DS_UPLOAD_FLAG in non volatile memory...................................160

Table 110 – From STARTUP to OPERATE (V1.0)...............................................................162

Table 111 – From STARTUP to OPERATE - interleave (V1.0)............................................163

Table 112 – Events – PDInvalid / PDValid (V1.0)................................................................164

Table 113 – MasterCycleTime.............................................................................................165

Table 114 – MinCycleTime..................................................................................................166

Table 115 – M-sequenceCapability.....................................................................................167

Table 116 – RevisionID.......................................................................................................168

Table 117 – ProcessDataIn.................................................................................................169

Table 118 – ProcessDataOut..............................................................................................170

Table 119 – VendorID.........................................................................................................171

Table 120 – DeviceID.........................................................................................................172

Table 121 – FunctionID.......................................................................................................173

Version V1.1.5 – 16 – Test Specification © IO-Link

Table 122 – Reserved parameter – Read/Write...................................................................174

Table 123 – Validity of MinCyleTime...................................................................................175

Table 124 – System command – reserved commands.........................................................177

Table 125 – Data Storage Index – entire parameter structure.............................................178

Table 126 – DataStorageIndex – record items....................................................................179

Table 127 – DataStorageIndex – termination marker...........................................................180

Table 128 – Device Access Locks – valid............................................................................181

Table 129 – Device Access Locks – invalid.........................................................................182

Table 130 – Profile Characteristic.......................................................................................183

Table 131 – PD Input Descriptor.........................................................................................184

Table 132 – PD Output Descriptor......................................................................................185

Table 133 – Vendor Name..................................................................................................186

Table 134 – Vendor Text.....................................................................................................187

Table 135 – Product Name.................................................................................................188

Table 136 – Product ID.......................................................................................................189

Table 137 – Product Text....................................................................................................190

Table 138 – SerialNumber..................................................................................................191

Table 139 – HardwareRevision...........................................................................................192

Table 140 – Firmware Revision...........................................................................................193

Table 141 – Application Specific Tag – valid.......................................................................194

Table 142 – Application Specific Tag – invalid....................................................................195

Table 143 – Error Count.....................................................................................................196

Table 144 – DeviceStatus...................................................................................................197

Table 145 – Detailed Device Status – complete object........................................................198

Table 146 – Detailed Device Status – Event active.............................................................199

Table 147 – Process Data Input..........................................................................................200

Table 148 – Process Data Output.......................................................................................201

Table 149 – Offset Time – valid..........................................................................................202

Table 150 – Profile Parameter – Read access....................................................................203

Table 151 – Profile Parameter – Write access....................................................................204

Table 152 – Write access – Read only parameter...............................................................205

Table 153 – Write access – Length too short......................................................................206

Table 154 – Write access – Length too long........................................................................207

Table 155 – Block parameter – Download...........................................................................208

Table 156 – Block parameter – Break by command.............................................................209

Table 157 – Block parameter – Break by reset....................................................................210

Table 158 – Block parameter – Illegal parameter write........................................................211

Table 159 – Block parameter – Break by double download.................................................212

Table 160 – Block parameter – local locking.......................................................................213

Table 161 – ParameterManager – Unexpected commands in Idle.......................................214

Table 162 – ParameterManager – Write request during Upload..........................................215

Table 163 – ParameterManager – Read requests during Download....................................216

Table 164 – ParameterManager – Unexpected commands during Upload...........................217

Test Specification © IO-Link – 17 – Version V1.1.5

Table 165 – ParameterManager – Unexpected switches Upload/Download.........................218

Table 166 – ParameterManager – Upload interrupted by reset............................................219

Table 167 – ParameterManager – UploadEnd during Download..........................................220

Table 168 – IODD identification..........................................................................................222

Table 169 – IODD communication parameter verification....................................................223

Table 170 – IODD parameter read verification....................................................................224

Table 171 – IODD parameter write verification....................................................................225

Table 172 – IODD parameter Index/Subindex consistency..................................................227

Table 173 – IODD – functional verification of "Parameter (write) access lock".....................230

Table 174 – IODD – functional verification of "Device reset"...............................................231

Table 175 – IODD – functional verification of "Application reset".........................................233

Table 176 – IODD – functional verification of "Restore factory settings"..............................235

Table 177 – IODD – functional verification of "Back-to-box"................................................237

Table 178 – IODD – Write alternative valid DeviceID..........................................................239

Table 179 – IODD – Write alternative invalid DeviceID........................................................240

Table 180 – Delay times after WURQ and Master messages (TDMT)..................................241

Table 181 – Delay time between three WURQs (TDWU).....................................................243

Table 182 – Number of WURQs..........................................................................................244

Table 183 – Delay time between WURQ retry sequences (TSD).........................................245

Table 184 – Delay time between two Master messages at STARTUP (TINITCYC)..............246

Table 185 – Delay time of Master messages with 2 octets OD in PREOP (TINITCYC)........247

Table 186 – Delay time of Master messages with 8 octets OD in PREOP (TINITCYC)........248

Table 187 – Delay time of Master messages with 32 octets OD in PREOP (TINITCYC)......249

Table 188 – Adjustment of the MasterCycleTime................................................................250

Table 189 – Written MasterCycleTime corresponds to real cycle time.................................251

Table 190 – Master tolerates different Device response times.............................................252

Table 191 – Master tolerates different UART frame delay times (T2)..................................253

Table 192 – Master sends UART frames within tolerated times (T1)...................................254

Table 193 – TYPE_2_1 for 8 bit PD input............................................................................255

Table 194 – TYPE_2_2 for 16 bit PD input..........................................................................256

Table 195 – TYPE_2_3 for 8 bit PD output..........................................................................257

Table 196 – TYPE_2_4 for 16 bit PD output........................................................................258

Table 197 – TYPE_2_5 for 8/8 bit PD in/output...................................................................259

Table 198 – TYPE_2_ V for 16/16 bit PD in/output and 1 octet OD.....................................260

Table 199 – TYPE_1_1 for 32 octets PD in and 2 octets OD...............................................262

Table 200 – TYPE_2_V for 0/32 octets PD in/out and variable octets OD............................264

Table 201 – TYPE_2_V for 32/0 octets PD in/out and variable octets OD............................265

Table 202 – Master reads mirrored PD in/out from Device..................................................266

Table 203 – Master propagates "PD invalid" indication in a correct manner........................267

Table 204 – Master propagates "PD valid" indication in a correct manner...........................268

Table 205 – TYPE_2_V for different PD in/out and 1 octet OD............................................269

Table 206 – TYPE_2_V for different PD in/out and 2 octets OD..........................................271

Table 207 – TYPE_2_V for different PD in/out and 8 octets OD..........................................273

Version V1.1.5 – 18 – Test Specification © IO-Link

Table 208 – TYPE_2_V for different PD in/out and 32 octets OD........................................274

Table 209 – Master sends Idle after an accomplished ISDU service....................................276

Table 210 – Master reads communication parameters (Direct Parameter)...........................277

Table 211 – Master adjusts to protocol V1.1 (Direct Parameter)..........................................278

Table 212 – Master adjusts to protocol V1.0 (Direct Parameter)..........................................279

Table 213 – Master start-up with non configured VID and DID............................................280

Table 214 – Master start-up with configured VID and DID...................................................281

Table 215 – Master start-up with overwrite of the DID (compatible).....................................282

Table 216 – Master start-up with overwrite of the DID (incompatible)..................................283

Table 217 – Master start-up with overwrite of the RID (incompatible)..................................284

Table 218 – Master start-up with non configured VID and DID (V1.0)..................................286

Table 219 – Master start-up with configured VID and DID (V1.0).........................................287

Table 220 – Master start-up with incorrect DID (V1.0).........................................................288

Table 221 – Master start-up with overwrite of the RID (compatible).....................................289

Table 222 – Master in PREOPERATE uses TYPE_0 to read Index 24 (1 OD).....................290

Table 223 – Master in PREOPERATE uses TYPE_1_2 to read Index 24 (2OD)..................291

Table 224 – Master in PREOPERATE uses TYPE_1_V to read Index 24 (8 OD).................292

Table 225 – Master in PREOPERATE uses TYPE_1_V to read Index 24 (32 OD)...............293

Table 226 – Master in PREOPERATE uses TYPE_0 to write Index 24 (1 OD).....................294

Table 227 – Master in PREOPERATE uses TYPE_1_2 to write Index 24 (2 OD).................295

Table 228 – Master in PREOPERATE uses TYPE_1_V to write Index 24 (8 OD).................296

Table 229 – Master in PREOPERATE TYPE_1_V to write Index 24 (32 OD).......................297

Table 230 – Master in OPERATE uses TYPE_1_V for zero bit PD in/out (8 OD).................298

Table 231 – Master in OPERATE uses TYPE_1_V for zero bit PD in/out (32 OD)...............299

Table 232 – Master in OPERATE uses TYPE_0 to read Index 24 (1 OD)............................300

Table 233 – Master in OPERATE uses TYPE_0 to write Index 24 (1 OD)............................301

Table 234 – Master in OPERATE uses TYPE_1_2 to write Index 24 (2 OD)........................302

Table 235 – Fallback from PREOPERATE..........................................................................303

Table 236 – Fallback from PREOPERATE fails...................................................................304

Table 237 – Fallback from OPERATE.................................................................................305

Table 238 – Fallback from OPERATE fails..........................................................................306

Table 239 – Retries after 2 reply messages with incorrect Checksum (STARTUP)..............307

Table 240 – Retries after 2 reply messages with incorrect Checksum (PREOP)..................308

Table 241 – Retries after 2 reply messages with incorrect Checksum (OPERATE)..............309

Table 242 – Retries after 3 reply messages with incorrect Checksum (STARTUP)..............310

Table 243 – Retries after 3 reply messages with incorrect Checksum (PREOP)..................311

Table 244 – Retries after 3 reply messages with incorrect Checksum (OPERATE)..............312

Table 245 – Retries after 2 missing replies and finally correct Checksum (STARTUP)........313

Table 246 – Retries after 2 missing replies and finally correct Checksum (PREOP)............314

Table 247 – Retries after 2 missing replies and finally correct Checksum (OPERATE)........315

Table 248 – Retries after 3 missing replies ending with restart/stop (STARTUP).................316

Table 249 – Retries after 3 missing replies ending with restart/stop (PREOP).....................317

Table 250 – Retries after 3 missing replies ending with restart/stop (OPERATE)................318

Test Specification © IO-Link – 19 – Version V1.1.5

Table 252 – Maximum WURQs and no final success...........................................................319

Table 253 – ISDU Write rejected with ErrorType.................................................................320

Table 254 – ISDU Write to unsupported Index rejected with ErrorType...............................321

Table 255 – ISDU Write to unsupported Subindex rejected with ErrorType.........................322

Table 256 – ISDU Write to temporarily unavailable Index rejected with ErrorType...............323

Table 257 – ISDU Write to temporarily unavailable Index due to local control.....................324

Table 258 – ISDU Write to temporarily unavailable Index due to Device control..................325

Table 259 – ISDU Write to read-only Index denied..............................................................326

Table 260 – ISDU Write with invalid Length........................................................................327

Table 261 – ISDU Write with parameter value out of range.................................................328

Table 262 – ISDU Write with parameter value above limit...................................................329

Table 263 – ISDU Write with parameter value below limit...................................................330

Table 264 – ISDU Write with invalid parameter set.............................................................331

Table 265 – ISDU Write while Device application fault........................................................332

Table 266 – ISDU Write to reserved Indices........................................................................333

Table 267 – ISDU Write to reserved Indices and no ISDU...................................................334

Table 268 – ISDU Write response without busy indication...................................................335

Table 269 – ISDU Write response with timeout after busy indication...................................336

Table 270 – ISDU Write response with illegal service code.................................................337

Table 271 – ISDU Write response with wrong checksum (CHKPDU)...................................338

Table 272 – ISDU Write response with reserved data length...............................................339

Table 273 – ISDU Read response without data...................................................................340

Table 274 – ISDU Write with minimum data length (zero octets).........................................341

Table 275 – ISDU Write with maximum service length (238 octets).....................................342

Table 276 – ISDU Read with maximum service length (238 octets).....................................343

Table 277 – ISDU Write to 8-bit Index and no Subindex......................................................344

Table 278 – ISDU Write to 8-bit Index and 8-bit Subindex...................................................345

Table 279 – ISDU Write to 16-bit Index and 8-bit Subindex.................................................346

Table 280 – ISDU Write response without busy bit..............................................................347

Table 281 – ISDU Write response with busy bit...................................................................348

Table 282 – ISDU Write with maximum service Length (15 octets)......................................349

Table 283 – ISDU Write with minimum service Extended Length (17).................................350

Table 284 – Master receives Events without details (notification)........................................351

Table 285 – Master receives event with details (single event).............................................352

Table 286 – Master receives event with details (double event)............................................353

Table 287 – Events of type notification...............................................................................355

Table 288 – Master receives Event with details (six Events)...............................................356

Table 289 – Master receives Event while in ISDU Write transfer (stopover)........................358

Table 290 – Master receives Event while in ISDU Read transfer (stopover)........................359

Table 291 – Master receives Event details while in ISDU Write transfer (stopover).............360

Table 292 – Master receives Event details while in ISDU Read transfer (stopover).............361

Table 293 – Behavior and Port Event at Device lost (COMLOST).......................................362

Table 294 – Event in PREOPERATE...................................................................................364

Version V1.1.5 – 20 – Test Specification © IO-Link

Table 295 – Event in PORT_DIAG......................................................................................365

Table 296 – Invalid cycle time requested............................................................................366

Table 297 – Delete stored DS object after reconfiguration..................................................367

Table 298 – Data Storage size limits (quantities)................................................................369

Table 299 – Write consistent DS data object from parameter server...................................370

Table 300 – Write inconsistent DS data object from parameter server.................................371

Table 301 – Write DS data object to improper Port configuration........................................372

Table 302 – Upload request Event in OPERATE (Backup + Restore)..................................373

Table 303 – Upload request Event in OPERATE (Restore)..................................................374

Table 304 – Upload request Event in OPERATE (Backup + Restore inactive).....................375

Table 305 – Start-up with empty DS (DS deactivated).........................................................376

Table 306 – Start-up with empty DS (DS activated – DS Upload)........................................377

Table 307 – Start-up with stored DS (Device replacement – DS Download)........................378

Table 308 – Start-up with stored DS (Device replacement – no DS Download)....................379

Table 309 – Start-up with stored DS (Device replacement – DS Upload).............................380

Table 310 – Start-up with stored DS (Device replacement – no DS Upload)........................381

Table 311 – Corrupted DS Index_List.................................................................................382

Table 312 – DS Download fault...........................................................................................384

Table 313 – DS Upload fault...............................................................................................386

Table 314 – Incompatible Device and DS............................................................................388

Table 315 – Master power OFF/ON (non-volatile DS data)..................................................390

Table 316 – DS Upload trial with locked Device Data Storage.............................................391

Table 317 – Master detects legacy Device and establishes connection...............................392

Table 318 – Master detects legacy Device and establishes interleave mode.......................393

Table 319 – Master receives an Event without details (Warning).........................................394

Table 320 – ISDU Write interrupted by an Event leads to a Write error...............................395

Table 321 – Master transforms PD_invalid Event into appropriate propagation...................396

Table A.1 – Mandatory features of a Reference-Master......................................................413

Table A.2 – Mandatory features of a Reference-Device......................................................414

Table A.3 – Requirements for the Device-Tester-System....................................................415

Table A.4 – System requirements for the Master-Tester......................................................417

Table A.5 – Functional requirements for the performance of test cases...............................418

Table A.6 – Preconditions of the EUT.................................................................................420

Table A.7 – Preconditions of the MTU.................................................................................420

Table A.8 – TestMacros of the Master-Tester-Program.......................................................421

Table A.9 – Event functions for Event tester handling.........................................................421

Table A.10 – ArgBlock Parameter Sets (ABPS)...................................................................422

Table A.11 – MTU instructions............................................................................................423

Table A.12 – MTU Index lists for Data Storage tests...........................................................426

Table A.13 – MTU parameter sets.......................................................................................427

Table A.14 – MTU specialties.............................................................................................427

Table C.1 – Test cases sorted by IDs.................................................................................430

Test Specification © IO-Link – 21 – Version V1.1.5

Test Specification © IO-Link – 23 – Version V1.1.5

1 Introduction

1. 1 General

This document together with its parent IEC 61131-9 is part of a series of standards on programmable controllers and the associated peripherals and should be read in conjunction with the other parts of the series.

IEC 61131-9 specifies the Single-Drop digital Communication Interface (IO-Link™

1. technology

as a generic interface for connecting digital/analog sensors and actuators to a Master unit, which may be combined with gateway capabilities to become a fieldbus remote I/O node.

The SDCI physical interface is backward compatible with the usual 24 V I/O signalling specified in IEC 61131-2 and allows in addition digital point-to-point communication at transmission rates of 4,8 kbit/s, 38,4 kbit/s and 230,4 kbit/s.

The SDCI technology specifies parameterization, cyclic exchange of process data, and diagnosis as well as parameter Data Storage capabilities.

This subpart specifies the test cases and associated test environments for SDCI Master and Devices designed and developed according to IEC 61131-9. It provides the necessary preconditions for conformity testing to ensure interoperability and enables manufacturers of Master and Devices to sign a corresponding conformity declaration.

The structure of this document is described in clause 5.2.

Where a conflict exists between this and other IEC standards (except basic safety standards), the provisions of this standard should be considered to govern in the area of programmable controllers and their associated peripherals.

Conformity with IEC 61131-9 cannot be claimed unless the requirements of this document are fulfilled.

Terms of general use are defined in IEC 61131-1 or in the IEC 60050 series. More specific terms are defined in each part.

1. 2 Patent declaration

There are no known patents related to the content of this document.

Attention is drawn to the possibility that some of the elements of this document may be the subject of patent rights. The IO-Link Community shall not be held responsible for identifying any or all such patent rights.

————————— 1 IO-Link TM is a trade name of the "IO-Link Community". Compliance to this standard does not require use of the registered logos for IO-Link TM . Use of the registered logos for IO-Link TM requires permission of the "IO-Link Community".

Test Specification © IO-Link – 25 – Version V1.1.5

### 1 PROGRAMMABLE CONTROLLERS —

2

### 3 Test specification for Master and Devices according to IEC 61131-9

### 4 (Single-drop Digital Communication Interface - SDCI)

5

6 2 Scope and objectives

7 The single-drop digital communication interface (SDCI) technology described in part 9 of the 8 IEC 61131 series focuses on simple sensors and actuators in factory automation, which are 9 nowadays using small and cost-effective microcontrollers. With the help of the SDCI technology, 10 the existing limitations of traditional signal connection technologies such as switching 0/24 V, 11 analog 0 to 10 V, etc. can be turned into a smooth migration. Classic sensors and actuators are 12 usually connected to a fieldbus system via input/output modules in so-called remote I/O periph- 13 erals. The (SDCI) Master function enables these peripherals to map SDCI Devices onto a 14 fieldbus system or build up direct gateways. Thus, parameter data can be transferred from the 15 PLC level down to the sensor/actuator level and diagnosis data transferred back in turn by 16 means of the SDCI communication. This is a contribution to consistent parameter storage and 17 maintenance support within a distributed automation system. SDCI is compatible to classic sig- 18 nal switching technology according to part 2 of the IEC 61131 series.

19 This subpart specifies the test cases and associated test environments for Master and Devices 20 designed and developed according to [1]. The objectives for the specification have been to

21 • provide a test coverage guaranteeing interoperability of Master and Device,

22 • protect Master and Devices from destruction or disturbance through operation with partner 23 devices,

24 • enable manufacturers of Master and Devices to sign a corresponding "manufacturer decla- 25 ration".

26 It was not an objective, to care for

27 • Interoperability of Master-Tools and Master,

28 • Test of the specific technology of a Device (for example profile tests),

29 • Stress test of multiple Master Port operations,

30 • Compliance with standards except for EMC.

31 3 Normative references

32 The following referenced documents are indispensable for the application of this document. For 33 dated references, only the edition cited applies. For undated references, the latest edition of 34 the referenced document (including any amendments) applies.

35 IEC 60947-5-2, Low-voltage switchgear and controlgear – Part 5-2: Control circuit Devices and 36 switching elements – Proximity switches

37 IEC 61131-2, Programmable controllers – Part 2: Equipment requirements and tests

38 IEC 61131-9, Programmable controllers – Part 9: Single-drop digital communication interface 39 for small sensors and actuators (SDCI)

40 4 Terms, definitions, symbols, abbreviated terms and conventions

41 4.1 Terms and definitions

42 For the purposes of this document, the following terms and definitions in addition to those given 43 in IEC 61131-1, IEC 61131-2, and IEC 61131-9 apply.

Version V1.1.5 – 26 – Test Specification © IO-Link

44 4.1.1 45 address 46 part of the M-sequence control to reference data within data categories of a communication 47 channel

48 4.1.2 49 application layer (AL) 50 <SDCI> part of the protocol responsible for the transmission of Process Data objects and On- 51 Request Data objects

52 4.1.3 53 block parameter 54 consistent parameter access via multiple Indices or Subindices

55 4.1.4 56 checksum 57 <SDCI> complementary part of the overall data integrity measures in the data link layer in ad- 58 dition to the UART parity bit

59 4.1.5 60 CHKPDU 61 integrity protection data within an ISDU communication channel generated through XOR pro- 62 cessing the octets of a request or response

63 4.1.6 64 coded switching 65 SDCI communication, based on the standard binary signal levels of IEC 61131-2

66 4.1.7 67 COM1 68 SDCI communication mode with transmission rate of 4,800 kbit/s

69 4.1.8 70 COM2 71 SDCI communication mode with transmission rate of 38,400 kbit/s

72 4.1.9 73 COM3 74 SDCI communication mode with transmission rate of 230,400 kbit/s

75 4.1.10 76 COMx 77 one out of three possible SDCI communication modes COM1, COM2, or COM3

78 4.1.11 79 communication error 80 unexpected disturbance of the SDCI transmission protocol

81 4.1.12 82 cycle time 83 time to transmit a frame between a Master and its Device including the following idle time

84 4.1.13 85 communication channel 86 logical connection between Master and Device

87 NOTE Four communication channels are defined: process channel, page and ISDU channel (for parameters) and 88 diagnostic channel. 89 4.1.14 90 Device 91 single passive peer to a Master such as a sensor or actuator

92 NOTE Uppercase "Device" is used for SDCI equipment, while lowercase "Device" is used in a generic manner.

Test Specification © IO-Link – 27 – Version V1.1.5

93 4.1.15 94 direct parameters 95 directly (page) addressed parameters transferred acyclically via the page communication chan- 96 nel without acknowledgement

97 4.1.16 98 dynamic parameter 99 part of a Device's parameter set defined by on-board user interfaces such as teach-in buttons 100 or control panels in addition to the static parameters

101 4.1.17 102 event 103 an instance of a change of conditions

104 NOTE An event is indicated via the event flag within the Device's status cyclic information, then acyclic transfer of 105 event data (typically diagnostics information) is conveyed through the diagnostic communication channel. 106 [IEC 61158-5-x, modified]

107 4.1.18 108 fallback 109 transition of a port from coded switching to switching signal mode

110 4.1.19 111 framing error 112 perturbed UART frames (physical layer)

113 4.1.20 114 interleave 115 segmented cyclic data exchange for process data with more than 2 octets through subsequent 116 cycles

117 4.1.21 118 ISDU 119 indexed service data unit used for acyclic acknowledged transmission of parameters that can 120 be segmented in a number of M-sequences

121 4.1.22 122 Legacy-Device 123 Device developed according to version V1.0 [5], the predecessor of [1]

124 4.1.23 125 Legacy-Master 126 Master developed according to version V1.0 [5], the predecessor of [1]

127 4.1.24 128 Master 129 active peer connected through ports to one up to n Devices and which provides an interface to 130 the gateway to the upper-level communication systems or PLCs

131 NOTE Uppercase "Master" is used for SDCI equipment, while lowercase "Master" is used in a generic manner. 132 4.1.25 133 message 134 <SDCI> coherent set of data octets transferred either from a Master to its Device or vice versa 135 following the rules of the SDCI protocol

136 4.1.26 137 M-sequence 138 sequence of two messages comprising a Master message and its subsequent Device message

139 4.1.27 140 M-sequence control 141 first octet in a Master message indicating the read/write operation, the type of the communica- 142 tion channel, and the address, for example offset or flow control

Version V1.1.5 – 28 – Test Specification © IO-Link

143 4.1.28 144 M-sequence error 145 unexpected or wrong message content, or no response

146 4.1.29 147 M-sequence type 148 one particular M-sequence format out of a set of specified M-sequence formats

149 4.1.30 150 on-request data 151 acyclically transmitted data upon request of the Master application consisting of parameters or 152 event data

153 4.1.31 154 PHY-3W (IEC 61131-9 → 3-wire system) 155 three wire connection to Devices for power, ground, communication and/or switching signals 156 defined in IEC 60947-5-2

157 4.1.32 158 physical layer 159 part of the communication protocol concerned with transmitting raw bits over a communication 160 channel

161 NOTE Physical layer provides means for wake-up and fallback procedures. 162 4.1.33 163 port 164 communication medium interface of the Master to one Device

165 4.1.34 166 port operating mode 167 state of a Master's port that can be either INACTIVE, DO, DI, SDCI, or ScanMode

168 4.1.35 169 process data 170 input or output values from or to a discrete or continuous automation process cyclically trans- 171 ferred with high priority and in a configured schedule automatically after start-up of a Master

172 4.1.36 173 process data cycle 174 complete transfer of all process data from or to an individual Device that may comprise several 175 cycles in case of segmentation (interleave)

176 4.1.37 177 single parameter 178 independent parameter access via one single Index or Subindex

179 4.1.38 180 SIO 181 port operation mode in accordance with digital input and output defined in IEC 61131-2 that is 182 established after power-up or fallback or unsuccessful communication attempts

183 4.1.39 184 static parameter 185 part of a Device's parameter set to be saved in a Master for the case of replacement without 186 engineering tools

187 4.1.40 188 switching signal 189 binary signal from or to a Device when in SIO mode (as opposed to the "coded switching" SDCI 190 communication)

### Test Specification © IO-Link – 29 – Version V1.1.5

### 191 4.1.41

### 192 system management (SM)

### 193 <SDCI> means to control and coordinate the internal communication layers and the exceptions

### 194 within the Master and its ports, and within each Device

### 195 4.1.42

### 196 UART frame

### 197 <SDCI> bit sequence starting with a start bit, followed by eight bits to carry a data octet, fol-

### 198 lowed by an even parity bit and ending with one stop bit

### 199 4.1.43

### 200 wake-up

### 201 procedure for causing a Device to change its mode from SIO to SDCI

### 202 4.1.44

### 203 wake-up request (WURQ)

### 204 physical layer service used by the Master to initiate wake-up of a Device, and put it in a receive

### 205 ready state

### 206 4.2 Symbols and abbreviated terms

fDTR Permissible deviation from data transfer rate, measured in %

PS Power supply ripple, measured in V

AL Application Layer

BEP Bit error probability

C/Q Connection for communication (C) or switching (Q) signal (SIO)

CLeff Effective total cable capacity, measured in nF

CQ Input capacity at C/Q connection, measured in nF

DI Digital input

DL Data Link Layer

DO Digital output

DPP1 Direct Parameter Page 1

DPP2 Direct Parameter Page 2

DTU Device tester unit

fDTR Data transfer rate, measured in bit/s

H/L High/low signal at receiver output

ICS Current sink for testing

I/O Input / output

ILL Input load current at input C/Q to V0, measured in A

IQ Driver current in saturated operating status ON, measured in A

IQH Driver current on high-side driver in saturated operating status ON, measured in A

IQL Driver current on low-side driver in saturated operating status ON, measured in A

IQPK Maximum driver current in unsaturated operating status ON, measured in A

IQPKH Maximum driver current on high-side driver in unsaturated operating status ON, measured in A

IQPKL Maximum driver current on low-side driver in unsaturated operating status ON, measured in A

IQQ Quiescent current at input C/Q to V0 with inactive output drivers, measured in A

IQWU Amplitude of Master’s wake-up request current, measured in A

IS Supply current at V+, measured in A

ISIR Current pulse supply capability at V+, measured in A

### Version V1.1.5 – 30 – Test Specification © IO-Link

LED Light emitting diode

L- Ground connection

L+ Power supply connection

MTU Master tester unit

NRZ Non return to zero

nWU Wake-up retry count

On/Off Driver’s ON/OFF switching signal

ON-REQ On-request data

OVD Signal Overload Detect

PDCT Port and Device configuration tool

PL Physical layer

PLC Programmable logic controller

PLT Physical layer test equipment

PS Power supply, measured in V

PSM Power supply of the Master ("24 V" mains or other)

r Time to reach a stable level with reference to the beginning of the start bit, measured in TBIT

RLeff Loop resistance of cable, measured in Ω

s Time to exit a stable level with reference to the beginning of the start bit, measured in TBIT

SDCI Single-drop digital communication interface

SIO Standard Input Output (digital switching mode) [IEC 61131-2]

SM System Management

t1 Character transfer delay on Master, measured in TBIT

t2 Character transfer delay on Device, measured in TBIT

tA Response delay on Device, measured in TBIT

TBIT Bit time, measured in s

tCYC Cycle time on M-sequence level, measured in s

tDF Fall time, measured in s

TDMT Delay time while establishing Master port communication, measured in TBIT

tDR Rise time, measured in s

TDSIO Delay time on Device for transition to SIO mode following wake-up request, measured in s

TDWU Wake-up retry delay, measured in s

t M-sequence M-sequence duration, measured in TBIT

tidle Idle time between two M-sequences, measured in s

tH Detection time for high level, measured in s

tL Detection time for low level, measured in s

tND Noise suppression time, measured in s

TOFS Temporal offset for process data processing on the Device with reference to start of cycle, measured in s

TPON Ramp-up time following power ON, measured in s

TRDL Wake-up readiness following power ON, measured in s

TREN Receive enable, measured in s

TSD Device detect time, measured in s

### Test Specification © IO-Link – 31 – Version V1.1.5

TWU Pulse duration of wake-up request, measured in s

UART Universal asynchronous receiver transmitter

UML Unified modelling language

V+ Voltage at L+

V0 Voltage at L-

VD- Voltage drop on the line between the L- connections on Master and Device, measured in V

VD+ Voltage drop on the line between the L+ connections on Master and Device, measured in V

VDQ Voltage drop on the line between the C/Q connections on Master and Device, measured in V

VHYS Hysteresis of receiver threshold voltage, measured in V

VI Input voltage at connection C/Q with reference to V0, measured in V

VIH Input voltage range at connection C/Q for high signal, measured in V

VIL Input voltage range at connection C/Q for low signal, measured in V

VRQ Residual voltage on driver in saturated operating status ON, measured in V

VRQH Residual voltage on high-side driver in operating status ON, measured in V

VRQL Residual voltage on low-side driver in saturated operating status ON, measured in V

VTH Threshold voltage of receiver with reference to V0, measured in V

VTHH Threshold voltage of receiver for safe detection of a high signal, measured in V

VTHL Threshold voltage of receiver for safe detection of a low signal, measured in V

WURQ Wake-up request pulse

207

### 208 4.3 Conventions

### 209 4.3.1 Test case template

### 210 This document uses a dedicated template as shown in Table 1 for the particular test cases. It

### 211 contains explanations on how to use items in left column.

### 212 Table 1 – Test case template

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_nnnn (nnnn = 4-digit consecutive number starting with 0001)

Name Characteristic name of the test case (see 4.3.1.1)

Purpose (short) Short description of the purpose of the test case (one line maximum)

Equipment under test (EUT) Master/Master + Port or Device

Test case version Starts with 1.0. Incremented first number indicates significant changes due to new functionality, the second one indicates changes within the test case

Category / type See 4.3.1.2

Specification (clause) [Bibliography, nn], clause or subclause, figure, table, chart, etc.

Configuration / setup For example, Master-Tester-System (see Figure A.20) shall detect all transmission rates and measure the corresponding delays. It shall not react to the requests. 213

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Comprehensive description of the purpose of the test case (can be several lines). Shall not contain preconditions or instructions.

Precondition Initial mode of the test set (both EUT and test environment) to be set prior to testing or ID of previous test. Examples:

### Version V1.1.5 – 32 – Test Specification © IO-Link

TEST CASE CONDITIONS / PERFORMANCE Tester precondition/Measurement instrument pre-set … EUT precondition …

Procedure - Step by step description of the test, each step marked by characters a), b), c), etc.

- Loops are possible.

- "Test step macros" are possible, shall be named "TS_<domain>_xxxx", and defined

within the general clause. Examples: a) Test step macro α b) Evaluation 1) c) Single instruction d) Evaluation 2) …

Test parameter - Shall be specified using definitions within [1]

- Can be identified using A), B), C), etc.

- Shall be linked to procedure steps, for example a), b), c), etc.

- Test loops can be used as specified in 4.3.1.3

Post condition Final mode of the EUT and its test environment. It is possible to keep evaluation results as input for subsequent test cases if a certain test case gets too complex. 214

TEST CASE RESULTS CHECK / REACTION

Evaluation - A sequence of steps, where the status of the EUT is checked at each step

- Each evaluation step is linked to a procedure step

- Each evaluation step to be marked by a numeric character 1), 2), 3), etc. Example:

1. Parameter β, Parameter γ, …

2. Value λ

…

Test passed - Approve reaction at each evaluation step whether it is correct ("AND").

- In case of alternate paths are defined, they shall be approved as defined ("OR").

- Approve if deviations can be tolerated as exceptions (see [8]).

Test not passed (examples) Describe incorrect reaction and describe the reasons for failing

Report Create brief data of test results such as measurement values, states, Events, implementation exceptions, test exceptions (see [8]), etc., and if test passed or not passed. Data shall be sufficient for a test certificate (option).

215

### 216 4.3.1.1 Name of a test case

### 217 Figure 1 shows the structure of the name of a test case.

218

### 219 Figure 1 – Structure of the test case name

### 220 4.3.1.2 Categories and types of test cases

### 221 Table 2 shows the used test case categories within this document.

### Test Specification © IO-Link – 33 – Version V1.1.5

### 222 Table 2 – Test case categories

Category Definition

Master PL test Measure port voltages, currents, and timings

Device PL test Measure Device voltages, currents, and timings

Master/Device signal test Transmission signal testing (eye diagram, UART frame, etc.)

Device protocol test Device protocol test (STARTUP, PREOPERATE, OPERATE)

Device ISDU test Device ISDU protocol test

Device Event test Device Event handling test

Device DS test Device Data Storage test

Device/legacy Master test Device operation on legacy Master

Device DPP test Test of Device's Direct Parameter page handling

Device application test Test of Device's behavior with single parameters

Device Block parameter test Test of Device's behavior with Block parameters

IODD parameter verification test Test of IODD/Device matching in parameter

IODD functional system test Test of IODD/Device matching in function

Master protocol test Master protocol test (timings, STARTUP, PREOPERATE, OPERATE)

Master M-sequence test Test of used Master M-sequence TYPEs (PD and OD)

Master robustness test Test of Master's fallback and retry behavior

Master ISDU test Test of Master's ISDU behavior (error types, limits)

Master Event test Test of Master's Event behavior (error, notification)

Master DS test Test of Master Data Storage mechanisms

Master legacy Device test Test of Master behavior with legacy Device

223

### 224 Table 3 shows the used test case types within this document

### 225 Table 3 – Test case types

Types Definition

Test to pass Positive test. A function shall perform as specified. Usually, the tests of a domain are beginning with these tests, where no stress is applied.

Test to fail Negative or stress test. A function shall react with a defined behavior, for example an error indication when boundary conditions are exceeded.

226

### 227 4.3.1.3 Use of test loops (TL)

### 228 One means to reduce the complexity of test case descriptions or the number of test cases is

### 229 using test loops (procedure iterations).

### 230 Test loops shall be limited to variations of procedure variables listed in field "Test parameter".

### 231 Branches in test case procedures shall not depend on these variables, e.g. "if variable = xyz

### 232 then…". Evaluations should be independent from procedure variables.

### 233 Variations of procedure variables are specified in field "Test parameter" as follows:

Test parameter config = {c1, c2, c3}

### 234 That means, the values c1, c2, c3 are sequentially assigned to the procedure variable "Config".

### 235 Within the procedure steps, the assignment of an actual list value shall be specified as shown

### 236 in the example below:

### Version V1.1.5 – 34 – Test Specification © IO-Link

Procedure e) … f) Initialize "config" with first value in list g) …

### 237 If more than one procedure variable is assigned in the same step, these variables shall be

### 238 separated by a comma. A test loop can be specified as shown in the example below:

Procedure i) … j) Repeat from g) with next value in list k) …

### 239 A complete example with assignments, evaluations, and test passed is shown below:

Procedure a) Assign first value to "mode" b) Assign first values to "config", "voltage" c) Procedure step using "config", "voltage", "mode" if needed d) Acquire results e) Evaluation 1) f) Repeat from c) with next "config", "voltage" g) Repeat from b) with next "mode"

Test parameter config = {c1, c2, c3}, voltage = {18V, 24V, 30V} mode = {mode1, mode2}

Evaluation 1) Check results (should be independent from procedure variables)

Test passed Result1 (mode1, config=c1, voltage=18 V) = conditions for test to pass Result2 (mode1, config=c2, voltage=24 V) = conditions for test to pass Result3 (mode1, config=c3, voltage=30 V) = conditions for test to pass Result4 (mode2, config=c1, voltage=18 V) = conditions for test to pass Result5 (mode2, config=c2, voltage=24 V) = conditions for test to pass Result6 (mode2, config=c3, voltage=30 V) = conditions for test to pass Hint: As shown above, different "test passed" conditions can be necessary if the conditions to pass a test are depending on certain procedure variables.

240

### 241 4.3.1.4 Handling of reports

### 242 Some test case reports require the presentation of measurement values others a flat summary.

### 243 Generally, for the designer of tester equipment it is possible to always indicate a flat summary

### 244 if all evaluations are positive (passed) and to show details if evaluations are negative.

### 245 4.3.2 Names of variables

### 246 Due to the possible implementation of the test cases in software, all used symbols and abbre-

### 247 viated terms in this document (see 4.2) are written in upper case letters without superscript or

### 248 subscript.

### 249 4.3.3 Memory and transmission octet order

### 250 Figure 2 demonstrates the order that shall be used when transferring WORD based data types

### 251 from memory to transmission and vice versa.

Test Specification © IO-Link – 35 – Version V1.1.5

252

253 Figure 2 – Memory and transmission octet order

254 4.3.4 Behavioral descriptions

255 For the behavioral descriptions the notations of UML 2 [2] are used, mainly timing diagrams.

5 Test strategy for SDCI (IO-Link TM2

256. Master and Devices

257 5.1 Purpose of this specification

258 This specification describes the test cases and specifies the necessary test equipment in con- 259 junction with its parent document IEC 61131-9 and updates from its support organization. The 260 functionality of Masters supporting legacy Devices according to [5] is covered also. Necessary 261 supplements or clarifications regarding [5] are covered in B.

262 This document provides the necessary information for the development of testers for a test suite 263 in test laboratories.

264 5.2 Structure of this document

265 Clause 6 describes the test cases for the physical layer test of Master and Devices. They mainly 266 require individual manual tests with variable power supplies, individual capacitive and resistive 267 loads, voltage, and current meters as well as oscilloscopes and logic analyzers.

268 Clause 7 describes the test cases for the Device protocol tests, which can be performed nearly 269 automatically via a Device-Tester-System (see Annex A.2).

270 Clause 8 describes the concepts of the XML schema and business rules tests for IODDs that 271 have been shifted to [3]. Dedicated test cases verify the consistency of IODD parameters and 272 the real parameters within the associated Device.

273 Clause 9 describes the test cases for the Master protocol tests, which can be performed auto- 274 matically via a Master-Tester-System (see Annex A.3).

275 Clause 10 defines the standards for the environmental tests of Master and Devices. A describes 276 the test tools, their requirements, and the test configurations. B contains a few supplementary 277 specifications filling the gaps of [5]. C provides cross reference listings for test case IDs and 278 test case names. D provides information about an SDCI support organization.

279 5.3 Conformity classes

280 5.3.1 Legacy Devices (V1.0)

281 Test cases for Devices designed and implemented according to [5] are no more supported by 282 this document.

————————— 2 IO-Link TM is a trade name of the "IO-Link Community". Compliance to this standard does not require use of the registered logos for IO-Link TM . Use of the registered logos for IO-Link TM requires permission of the "IO-Link Community".

0 Transmission time t

MSO LSO

MSO

LSO

n+4

n+3

n+2

n+1

n

Transmission

LSO

MSO

n+4

n+3

n+2

n+1

n

"Big endian" "Little endian"

Depending on the architecture of the microcontroller in use

Version V1.1.5 – 36 – Test Specification © IO-Link

283 5.3.2 Devices without ISDU

284 Devices designed and implemented according to [1] that are not supporting the ISDU feature 285 shall either require no parameters or provide a system conform mechanism for the Device re- 286 placement without tools. These Devices shall pass all test cases marked correspondingly in this 287 document.

288 5.3.3 Devices with ISDU

289 Devices designed and implemented according to [1] shall pass all test cases marked corre- 290 spondingly in this document. If they omit to implement the Data Storage mechanism according 291 to [1] they still shall provide access to Index 3 and shall provide system conform means for the 292 Device replacement without tools. These Devices shall pass all test cases marked correspond- 293 ingly in this document.

294 5.3.4 Legacy Master

295 Master solely designed and implemented according to [5] are no more supported by this docu- 296 ment. However, since Master designed and implemented according to [1] shall support "Legacy 297 Devices", all the corresponding test cases apply.

298 5.3.5 Master

299 Master designed and implemented according to [1] shall provide all the specified features. All 300 test cases within this document for Master apply.

301 5.4 Test of Devices

302 The test of Devices consists of four steps: Physical layer test, IODD test, protocol test, and 303 environmental test. The protocol tests of SDCI Devices can be performed by a Device-Tester- 304 System that shall be approved by the organization noted in D. The requirements for Device- 305 Tester-Systems are specified in Annex A.2.2.

306 Figure 3 shows step 1 of the Device test. It contains references to the relevant clauses in this 307 specification and consists of a visual check and manually performed measurements.

308 If the Device shows specific connectors, cables, or color codings, these deviations shall be 309 documented within the user manual with respect to the original definitions in the standard [1].

### Test Specification © IO-Link – 37 – Version V1.1.5

310

### 311 Figure 3 – Step 1 of the Device test sequence (PL)

### 312 If a Device did not pass a certain test case due to measurement values close to the tolerance

### 313 limits or similar situations it is possible to send an informal request to the organization listed in

### 314 D. This request shall be comprehensive enough for the experts to allow for an exception under

### 315 certain conditions or clarification of the specifications. If an exception applies, it shall be docu-

### 316 mented in the user manual mentioning the possible implications (see [8] for details).

### 317 Instead of pursuing such a time-consuming and uncertain way, it is highly recommended for the

### 318 applicant to rather strive for a robust implementation and conformity of the Device.

### 319 Figure 4 shows step 2 of the Device test. It contains references to the relevant clauses in this

### 320 specification and consists of an IODD-Test with a so-called IODD-Checker-Tool (8.2) and a

### 321 parameter verification test with the help of the protocol test (Device-Tester-System).

Setup test equipment

Perform test cases

Complete test report

Test case No.

All test cases terminated

Passed

Physical Layer test

Exception? Redesign, new test

Specific

No

Step 1: Physical Layer test

Description in user manual

Yes

Clause A.1.2 and A.1.3 (Measurement circuits)

Clauses 5.3, 5.5, 5.6 (Physical layer tests)

Preconditions: ⚫ Device (type "w/o ISDU" or "with ISDU") ⚫ IODD according [3] ⚫ Measurement instruments (volt/ampere meter, etc.) ⚫ Device-Tester-System

Clauses 5.7 (Test report template)

Step 2: IODD test

Device Test

Check connectors and cables

Description in user manual?

Standard

Clause 5.1 (General)

Not passed

### Version V1.1.5 – 38 – Test Specification © IO-Link

322

### 323 Figure 4 – Step 2 of the Device test sequence (IODD)

### 324 Figure 5 shows step 3 of the Device test. It contains references to the relevant clauses in this

### 325 specification and consists of an automated protocol test with the help of the Device-Tester-

### 326 System defined in A.2.2.

### 327 Two different sets of test cases are necessary to adjust the tests for the two Device conformity

### 328 classes: Devices without the ISDU feature developed according to [1], and Devices with ISDU

### 329 support developed according to [1].

Setup "Checker" tool

Step 2: IODD test

Clause 7.2 (XML schema and business rules check)

Step 3: Protocol tests

Run "Checker" tool

Schema check

Passed

Redesign, new test Not passed

Rules check

All rules checked

Redesign, new test Description in user manual Warning Not passed

Configure Device tester (via IODD)

Perform test cases

Complete test report

Test case No.

All test cases terminated

Passed

IODD related tests

Exception? Redesign, new test Not passed No

Description in user manual

Yes

Clause 7.5 (Test report template)

⚫ Clause 7.3 (Parameter verification test) ⚫ Clause 7.4 (Functional system tests)

Setup Device-Tester-System Clause A.2.2 (Device Tester-System)

### Test Specification © IO-Link – 39 – Version V1.1.5

330

### 331 Figure 5 – Step 3 of the Device test sequence (protocol)

### 332 The set of test cases for Devices without ISDU support is defined in Table 4.

### 333 Table 4 – Set of test cases for Devices without ISDU support

Major feature Test cases Remarks

STARTUP TC_0034 to TC_0038, TC_0306 New TC_0306 for test of revision management

PREOPERATE TC_0039 to TC_0041 TC_0043 to TC_0044 TC_0374, TC_0378

TC_0042 removed from list

OPERATE TC_0045 to TC_0047 TC_0049 TC_0312 and TC_0313 TC_0373, TC_0375, TC_0376

TC_0048 and TC_0051 removed from list

Events TC_0069 to TC_0076 Exceptions exist (see 7.6.1)

Legacy Master (V1.0) TC_0085 to TC_0087 If restricted to 134 ms; exceptions for PDInvalid exist (see Table 112)

Direct Parameter page 1 TC_0089 to TC_0097 TC_0101 TC_100 removed

334

### 335 The set of test cases for Devices with ISDU support is defined in Table 5.

Determine conformity class

V1.1 w/o ISDU No

Step 3: Protocol test

Configure Device tester (via IODD)

Yes

Clause 4.3 (Conformity classes)

Step 4: Environmental tests

Perform test cases

Print test report

Test case No.

All test cases terminated

Passed

Protocol test

Exception? Redesign, new test Not passed No

Description in user manual

Yes

Clause 6.12 (Test report generated by the Device Tester)

V1.1 with ISDU

Yes

Test cases listed in Table 4 Test cases listed in Table 5

### Version V1.1.5 – 40 – Test Specification © IO-Link

### 336 Table 5 – Set of test cases for Devices with ISDU support

Major feature Test cases Remarks

STARTUP TC_0034 to TC_0038 TC_0306 New TC_0306 for test of revision management

PREOPERATE TC_0039 to TC_0041 TC_0043 to TC_0044 TC_0377 TC_0374, TC_0378

TC_0042 removed from list

OPERATE TC_0045 to TC_0047 TC_0049 TC_0312 and TC_0313 TC_0373 , TC_0375 , TC_0376

TC_0048 and TC_0051 removed from list.

ISDU TC_0052 to TC_0068 TC_0309 New TC_0309 for repeated ISDU segments

Events TC_0069 to TC_0076 Exceptions exist (see 7.6.1)

Data Storage TC_0077 to TC_0084 TC_0321 to TC_0322 New TC_0321/322 for Parameter manager, upload flag

Legacy Master (V1.0) TC_0085 to TC_0087 If compatible; exceptions for PDInvalid exist (see Table 112)

Direct Parameter page 1 TC_0089 to TC_0097 TC_0101 TC_0100 removed

Predefined parameters TC_0104 TC_0323 TC_0107 to TC_0124 TC_0128 to TC_0129 TC_0131 to TC_0134 TC_0136 to TC_0137 TC_0140 to TC_0142

TC_0105 removed TC_0323 for DS termination marker

TC_0130 removed TC_0135 removed TC_0136/0137 for profile parameter

Block parameter TC_0143 to TC_0148 TC_0324 to TC_0330 TC_0146 reactivated New TC_0324 to TC_0330 for Parameter manager

IODD based tests TC_0149 to TC_0152 TC_0157 TC_0155 to TC_0156 TC_0314 to TC_0318

IODD parameter verification

New for functional system tests (reset, locking, etc.)

337

### 338 Figure 6 shows step 4 of the Device test. It contains references to the relevant clauses in this

### 339 specification and consists of an EMC test defined in [1] and possible tests according to relevant

### 340 product standards such as for example the IEC 60947 series. A successfully terminated Device

### 341 test can be completed by a manufacturer declaration (see D).

### Test Specification © IO-Link – 41 – Version V1.1.5

342

### 343 Figure 6 – Step 4 of the Device test sequence (environment)

### 344 5.5 Test of Masters

### 345 The test of Masters consists of four steps: Physical layer test, protocol test, IODD interpreter

### 346 test, and environmental test. The protocol tests of Master can be performed by a Master-Tester-

### 347 System approved by the organization noted in D. The requirements for Master-Tester-Systems

### 348 are specified in Annex A.3.

### 349 Figure 7 shows step 1 of the Master test sequence. It contains references to the relevant

### 350 clauses in this specification and consists of a visual check and manually performed measure-

### 351 ments.

### 352 If the Master shows specific connectors, cables, or color codings, these deviations shall be

### 353 documented within the user manual in respect to the original definitions in the standard [1].

### 354 If a Master did not pass a certain test case due to measurement values close to the tolerance

### 355 limits or similar situations it is possible to send an informal request to the organization listed in

### 356 D. This request shall be comprehensive enough for the experts to allow for an exception under

### 357 certain conditions or clarification of the specifications. If an exception applies, it shall be docu-

### 358 mented in the user manual mentioning the possible implications (see [8] for details).

### 359 Instead of pursuing such a time-consuming and uncertain way, it is highly recommended for an

### 360 applicant to rather strive for a robust implementation and conformity of the Master.

Setup EMC testing Clause 9.1 (General)

Step 4: Environmental tests

Execute tests

Complete test report

EMC test No.

All test cases terminated

Passed

Environmental test

Exception? Redesign, new test Not passed No

Description in user manual

Yes

Clause 9.4 (EMC Test report)

Complete manufacturer declaration

Other relevant standards? Execute tests Not passed Redesign, new test Yes

Annex G.4 in [9] (Manufacturer declaration)

End of Device testing

Passed No

### Version V1.1.5 – 42 – Test Specification © IO-Link

361

### 362 Figure 7 – Step 1 of the Master test sequence (PL)

### 363 Figure 8 shows step 2 of the Master test sequence. It contains references to the relevant

### 364 clauses in this specification and consists of an automated protocol test with the help of a Mas-

### 365 ter-Tester-System defined in Annex A.3.

Setup test equipment

Perform test cases

Complete test report

Test case No.

All test cases terminated

Passed

Physical Layer test

Exception? Redesign, new test

Specific

No

Step 1: Physical Layer test

Description in user manual

Yes

Clause A.1.2 and A1.3 (Measurement circuits)

Clauses 5.2, 5.4, 5.6 (Physical layer tests)

Preconditions: ⚫ Master (type "V1.1") ⚫ IODD interpreter according [3] and [19] ⚫ Measurement instruments (volt/ampere meter, etc.) ⚫ Master-Tester-System "Fictive IODD" for MTU ⚫ (Interpreter Test)

Clauses 5.7 (Test report template)

Step 2: Protocol test

Master Test

Check connectors and cables

Description in user manual

Standard

Clause 5.1 (General)

### Test Specification © IO-Link – 43 – Version V1.1.5

366

### 367 Figure 8 – Step 2 of the Master test sequence (PL)

### 368 The set of test cases for Masters (V1.1) is defined in Table 6.

### 369 Table 6 – Set of test cases for Masters (V1.1)

Major feature Test cases Remarks

Timings TC_0158 to TC_0167 TC_0331 to TC_0333 New TC_0331ff: delay time of Master messages

Process Data (PD) TC_0168 to TC_0173 TC_0298 TC_0334 to TC_0335

TC_0174/0175 removed

New TC_0334ff: TYPE_2_V

On-request Data (OD) TC_0179 to TC_0182 TC_0288 New TC_0288 for Idle after ISDU service

STARTUP TC_0183 to TC_0194 TC_0307 TC_0195 to TC_0201 removed New TC_0307 for test of revision management

PREOPERATE TC_0202 to TC_0209

OPERATE TC_0336 to TC_0337 TC_0210 to TC_0212 New TC_0336ff: TYPE_1_V

Fallback TC_0213 to TC_0216 TC_0213 and TC_0214 are optional

Retry TC_0338 to TC_0349 TC_0221 to TC_0222 New approach: TC_0217 to TC_0220 removed

ISDU (application errors) TC_0223 to TC_0235 TC_0237 to TC_0238 TC_0236 removed

Determine conformity class

V1.1 Master No

Step 2: Protocol test

Configure Master tester (via configuration file)

Clause 4.3 (Conformity classes)

Step 3: IODD interpreter test

Perform test cases

Print test report

Test case No.

All test cases terminated

Passed

Protocol test

Exception? Redesign, new test Not passed No

Description in user manual

Yes

Clause 8.16 (Test report generated by the Master Tester)

Yes

Test cases listed in Table 6

Setup Master Tester Clause A.3 (Master-Tester-System

Master not defined in this document

### Version V1.1.5 – 44 – Test Specification © IO-Link

Major feature Test cases Remarks

ISDU (derived errors) TC_0239 to TC_0242 TC_0244

ISDU (Limit checks) TC_0243 TC_0245 to TC_0246 TC_0248 to TC_0255

Events TC_0256 TC_0261 to TC_0262 TC_0308 TC_0263 to TC_0267 TC_0350 to TC_0353

New TC_0261ff for Events with details New TC_0308 for Notifications TC_0268/0269 removed New TC_0350ff for Events outside OPERATE

Data Storage TC_0354 to TC_0372 New approach: TC_0270 to TC_0284 removed

Legacy Device (V1.0) TC_0285 to TC_0290 TC_0291 removed

370

### 371 Figure 9 shows step 3 of the Master test sequence. It contains references to the relevant

### 372 clauses in this specification. The Master-Tester-System defined in Annex A.3 provides a "Fictive

### 373 IODD" (see 8.2.2) in accordance with [3]. The user manual of the tool demonstrates how this

### 374 IODD shall be presented to the user with respect to completeness and correctness.

375

### 376 Figure 9 – Step 3 of the Master test sequence (Interpreter)

### 377 Figure 10 shows step 4 of the Master test. It contains references to the relevant clauses in this

### 378 specification and consists of an EMC test defined in [1] and possible tests according to relevant

### 379 product standards such as for example the IEC 61131-2. A successfully terminated Master test

### 380 can be completed by a manufacturer declaration (see D).

Step 3: IODD Interpreter Test

⚫ Launch interpreter tool ⚫ Configure Master Port

Step 4: Environmental tests

Configure Master tester

Check completeness of Interpreter display

All tests terminated

No deviation

IODD Interpreter test

Redesign, new test No

Description in user manual

Yes

Setup IODD Interpreter

Clause 7.2.2 ("Fictive IODD")

User manual of Master-Tester-System and [3]

Deviation

Exception?

Check complete and correct parameter operations

No deviation

Redesign, new test No

Description in user manual

Yes

User manual of Master-Tester-System

Deviation

Exception?

Print test report Clause 8.16 Test report generated by the Master-Tester-System)

### Test Specification © IO-Link – 45 – Version V1.1.5

381

### 382 Figure 10 – Step 4 of the Master test sequence (environment)

383

### 384 6 Physical Layer (PL) tests

### 385 6.1 General

### 386 The physical layer tests comprise a visual inspection of the type of connector, cable, maximum

### 387 cable length and color coding of the wires. If customer specific or region specific connectors,

### 388 cable, and the color coding of the wires deviate from the specifications in [1], the user manual

### 389 of the Device shall document clearly the differences in comparison with the definitions in the

### 390 standard.

### 391 The physical layer tests comprise also measurements of voltage levels and currents as well as

### 392 timing limits, slopes and line and message signals. The necessary measurement instruments

### 393 are defined in Annex A.1.1 and the necessary measurement circuits are defined in Annex A.1.2.

### 394 Physical layer tests are carried out at room temperature (15 °C to 35 °C). However, the tests

### 395 shall pass within the whole operating temperature range for a Device specified by the manu-

### 396 facturer.

397

Setup EMC testing Clause 9.1 (General)

Step 4: Environmental tests

Execute tests

Complete test report

EMC test No.

All test cases terminated

Passed

Environmental test

Exception? Redesign, new test Not passed No

Description in user manual

Yes

Clause 9.4 (EMC Test report)

Complete manufacturer declaration

Other relevant standards? Execute tests Not passed Redesign, new test Yes

Annex G.4 in [9] (Manufacturer declaration)

End of Master testing

Passed No

Fieldbus conformity test ⚫ Relevant Fieldbus Integration specification ⚫ Fieldbus test laboratory

Fieldbus Integration test

### Version V1.1.5 – 46 – Test Specification © IO-Link

### 398 6.2 Static parameters of the Master interface

### 399 6.2.1 DC supply current capability of Master Port L+

### 400 Table 7 defines the test conditions for this test case.

### 401 Table 7 – DC supply current capability of Master Port L+

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0001

Name TCM_PHYL_INTF_VSMISMBEHAV

Purpose (short) DC supply voltage and current capability of Master Port

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.2.3, Table 6

Configuration / setup Variable Master input voltage PSM and variable current sink according to Figure A.3 402

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) DC supply voltage and current capability of Master Port L+ shall be within specified boundaries. Behavior of both quantities shall be monitored for a certain time.

Precondition PLT: Current sink between L+ and L- EUT: PORT_DI (see A.4.2)

Procedure a) Apply first PSM value to Master;Test parameter b) Adjust current sink to ISM value;Test parameter c) Monitor VSM and ISM at L+ for 30 s d) Evaluation 1) e) Repeat test with next PSM value and ISM value from c)

Test parameter PSM = {PSMmin, PSMmax} (according to user manual) ISM = {ISMmax, 0 mA} (ISMmax according to user manual)

Post condition − 403

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check VSM and ISM flow

Test passed 20 V ≤ VSM ≤ 30 V, AND ISM is constantly flowing

Test not passed (examples) VSM < 20 V, OR VSM > 30 V, OR ISM is interrupted

Report VSM (PSMmin): <value> <ok | nok> VSM (PSMmax): <value> <ok | nok> Steady current flow: <yes/no> <ok | nok>

404

405

### Test Specification © IO-Link – 47 – Version V1.1.5

### 406 6.2.2 Power-On supply current capability of Master Port L+

### 407 Table 8 defines the test conditions for this test case.

### 408 Table 8 – Power-On supply current capability of Master Port L+

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0002

Name TCM_PHYL_INTF_ISIRM

Purpose (short) Power-on/Port turn-on supply current capability at Master Port

Equipment under test (EUT) Master (see Annex B.2).

Test case version 1.3

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.2.3, Table 6

Configuration / setup Supply current at Master Port is monitored according to Figure A.3 409

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Driver capability of the Master Port L+ supply at power-on or switch-on of Port L+. Monitor supply current drawn from the Master Port.

Precondition PLT: Current sink between L+ and L-; current sink shall be configured to consume ≥ 1000 mA (ISIRM) EUT: PORT_INACTIVE (see A.4.2)

Procedure a) Set supply voltage of Master to first value of PSM;Test parameter b) Exemplary: SMI_PortPowerOffOn(ABPS_PORTPOWERON) (see A.4.6) c) Identify time tmin, where ISM reaches ISIRMmin = 400mA d) Monitor current ISIRM at L+ after tmin for 50 ms e) Evaluation 1) f) Repeat test from step b) with next PSM value;Test parameter

Test parameter PSM = {PSMmin, PSMmax} (according to user manual)

Post condition − 410

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Evaluate charge QMI using the formula 𝑄𝑀𝐼 = ∫ 𝐼𝑆𝑀(𝑡)𝑑𝑡 𝑡𝑚𝑖𝑛+50𝑚𝑠 𝑡𝑚𝑖𝑛

Test passed Charge QMI ≥ 20 mAs

Test not passed (examples) Charge QMI < 20 mAs (see Annex B.2)

Report QMI (PSMmin): <value> <ok | nok> QMI (PSMmax): <value> <ok | nok>

411

412

### Version V1.1.5 – 48 – Test Specification © IO-Link

### 413 6.2.3 Load current at Master Port C/Q

### 414 Table 9 defines the test conditions for this test case.

### 415 Table 9 – Load current at Master Port C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0003

Name TCM_PHYL_INTF_ILLM

Purpose (short) Load current at C/Q of Master Port

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.2.3, Table 6

Configuration / setup The input current at C/Q of the Master Port is monitored. 416

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Quiescent current at Master Port C/Q in input mode. Monitor current flowing into C/Q.

Precondition PLT: Voltage source with value VIM between L- and C/Q EUT: PORT_DI

Procedure a) Set supply voltage of Master to first PSM value;Test parameter b) Measure VSM c) Set voltage VIM = 5V d) Measure current ILLM into C/Q e) Evaluation 1) f) Set voltage VIM = 5,1 V g) Measure current ILLM into C/Q h) Evaluation 2) i) Set voltage VIM = 15V j) Measure current ILLM into C/Q k) Evaluation 3) l) Set voltage VIM = measured value of VSM in b) m) Measure current ILLM into C/Q n) Evaluation 4) o) Repeat from b) with next PSM value;Test parameter

Test parameter PSM = {PSMmin, PSMmax} (according to user manual)

Post condition − 417

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ILLM < 15 mA

2. Check 2 mA < ILLM < 15 mA

3. Check 2 mA < ILLM < 15 mA

4. Check 2 mA < ILLM < 15 mA

Test passed All checks OK

Test not passed (examples) One of the checks above failed

Report ILLM (VIM = 5 V, PSMmin): <value> <ok | nok> ILLM (VIM = 5,1 V, PSMmin): <value> <ok | nok> ILLM (VIM = 15 V, PSMmin): <value> <ok | nok> ILLM (VIM = measured value of VSM, PSMmin): <value> <ok | nok> ILLM (VIM = 5 V, PSMmax): <value> <ok | nok> ILLM (VIM = 5,1 V, PSMmax): <value> <ok | nok> ILLM (VIM = 15 V, PSMmax): <value> <ok | nok> ILLM (VIM = measured value of VSM, PSMmax): <value> <ok | nok>

418

419

### Test Specification © IO-Link – 49 – Version V1.1.5

### 420 6.2.4 High-side residual voltage at Master Port C/Q

### 421 Table 10 defines the test conditions for this test case.

### 422 Table 10 – High-side residual voltage at Master Port C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0004

Name TCM_PHYL_INTF_VRESHIGH

Purpose (short) Static high-side driver capability

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.2.3, Table 6

Configuration / setup The output level at Master C/Q output is measured 423

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Driver capability of the Master Port high-side driver. Measurement of the voltage drop between supply L+ and C/Q output.

Precondition PLT: Current sink IQHMmin between C/Q and L-; Voltage measurement for VRQHM between L+ and C/Q EUT: PORT_DO (see A.4.2)

Procedure a) Set supply voltage of Master to first PSM value;Test parameter b) SMI_PDOut(ABPS_DO_HIGH) (see A.4.6) c) Measure VRQHM d) Evaluation 1) e) Repeat from b) with next PSM value;Test parameter

Test parameter PSM = {PSMmin, PSMmax} (according to user manual)

Post condition − 424

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check VRQHM ≤ 3V

Test passed All checks OK

Test not passed (examples) Any of the checks above failed

Report VRQHM (PSMmin): <value> <ok | nok> VRQHM (PSMmax): <value> <ok | nok>

425

426

### Version V1.1.5 – 50 – Test Specification © IO-Link

### 427 6.2.5 Low-side residual voltage at Master Port C/Q

### 428 Table 11 defines the test conditions for this test case.

### 429 Table 11 – Low-side residual voltage at Master Port C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0005

Name TCM_PHYL_INTF_VRESLOW

Purpose (short) Static low-side driver capability

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.2.3, Table 6

Configuration / setup The output level at Master Port C/Q output is measured 430

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Driver capability of the Master Port low-side driver. Measurement of the voltage drop between C/Q output and supply L-.

Precondition PLT: Current source IQHMmin between L+ and C/Q; Voltage measurement for VRQLM between C/Q and L- EUT: PORT_DO (see A.4.2)

Procedure a) Set supply voltage of Master to first PSM value;Test parameter b) SMI_PDOut(ABPS_DO_LOW) (see A.4.6) c) Measure VRQLM d) Evaluation 1) e) Repeat from b) with next PSM value;Test parameter

Test parameter PSM = {PSMmin, PSMmax} (according to user manual)

Post condition − 431

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check VRQLM ≤ 3V

Test passed All checks OK

Test not passed (examples) Any of the checks above failed

Report VRQLM (PSMmin): <value> <ok | nok> VRQLM (PSMmax): <value> <ok | nok>

432

433

### Test Specification © IO-Link – 51 – Version V1.1.5

### 434 6.2.6 High-level input threshold voltage at Master Port C/Q

### 435 Table 12 defines the test conditions for this test case.

### 436 Table 12 – High-level input threshold voltage at Master Port C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0006

Name TCM_PHYL_INTF_VTHHM

Purpose (short) Test of static input high-level threshold at C/Q

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.2.2, Table 5

Configuration / setup The digital input signal for C/Q input is monitored (see Figure A.6) 437

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Measurement of the threshold voltage for high-level at the C/Q

Precondition PLT: Voltage source with value VIM between C/Q and L- EUT: PORT_DI (see A.4.2)

Procedure a) Set supply voltage of Master to first PSM value;Test parameter b) Sweep voltage VIM at C/Q from 5 V to 15 V in steps of maximum 0,1 V c) Exemplary: Repeat SMI_PDIn until DI_C/Q = 1; (see Reference for SMI: [1], 11.2.17, Figure 113) d) Measure VIM Transition e) Evaluation 1) f) Repeat from b) with next PSM value;Test parameter

Test parameter PSM = {PSMmin, PSMmax} (according to user manual)

Post condition Memorize VIM at DI transition 0→1 (all PSM) 438

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check voltage VIM at DI transition "low" to "high"

Test passed All checks: 10,5 V < VIM < 13 V (range of VTHHM)

Test not passed (examples) One of the checks failed

Report VIM @ Transition 0→1 (PSMmin): <value> <ok | nok> VIM @ Transition 0→1 (PSMmax): <value> <ok | nok>

439

440

### Version V1.1.5 – 52 – Test Specification © IO-Link

### 441 6.2.7 Low-level input threshold voltage at Master Port C/Q

### 442 Table 13 defines the test conditions for this test case.

### 443 Table 13 – Low-level input threshold voltage at Master Port C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0007

Name TCM_PHYL_INTF_VTHLM

Purpose (short) Static input low-level threshold at C/Q

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.2.2, Table 5

Configuration / setup The digital input signal for C/Q input is monitored (see Figure A.6) 444

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Measurement of the threshold voltage for low-level at C/Q.

Precondition PLT: Voltage source with value VIM between and C/Q and L- EUT: PORT_DI (see A.4.2)

Procedure a) Set supply voltage of Master to first PSM value;Test parameter b) Sweep voltage VIM at C/Q from 15 V to 5 V in steps of maximum 0,1 V c) Examplery: Repeat SMI_PDIn until DI_C/Q = 0; (see Reference for SMI: [1], 11.2.17, Figure 113) d) Measure VIM Transition e) Evaluation 1) f) Repeat from b) with next PSM value;Test parameter

Test parameter PSM = {PSMmin, PSMmax} (according to user manual)

Post condition Memorize VIM at DI transition 1→0 (all PSM) 445

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check voltage VIM at DI transition "high" to "low"

Test passed For all checks: 8.0 V < VIM < 11,5 V (range of VTHLM)

Test not passed (examples) One of the checks failed

Report VIM @ Transition 1→0 (PSMmin): <value> <ok | nok> VIM @ Transition 1→0 (PSMmax): <value> <ok | nok>

446

447

### Test Specification © IO-Link – 53 – Version V1.1.5

### 448 6.2.8 Input hysteresis voltage at Master Port C/Q

### 449 Table 14 defines the test conditions for this test case.

### 450 Table 14 – Input hysteresis voltage at Master Port C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0008

Name TCM_PHYL_INTF_VHYSM

Purpose (short) Calculation of input hysteresis at C/Q

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.2.2, Table 5

Configuration / setup See TC_0006 and TC_0007 451

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Calculation of the hysteresis voltage at C/Q.

Precondition Value VIM(TC_0006) from TC_0006 is available for all PSM values Value VIM(TC_0007) from TC_0007 is available for all PSM values

Procedure −

Test parameter −

Post condition − 452

TEST CASE RESULTS CHECK / REACTION

Evaluation For all PSM values: VHYSM = Value VIM(TC_0006) – Value VIM(TC_0007)

Test passed For all PSM values: Voltage VHYSM is ≥ 0 V

Test not passed (examples) For any PSM value: Voltage VHYSM is < 0 V

Report VHYSM (PSMmin): <value> <ok | nok> VHYSM (PSMmax): <value> <ok | nok>

453

454

455

### Version V1.1.5 – 54 – Test Specification © IO-Link

### 456 6.2.9 Permissible voltage range at Master Port C/Q

### 457 Table 15 defines the test conditions for this test case.

### 458 Table 15 – Permissible voltage range at Master Port C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0299

Name TCM_PHYL_INTF_VOLTRANGECQ

Purpose (short) Permissible voltage range at Master C/Q

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.2.2, Table 5, VIL and VIH

Configuration / setup See Precondition 459

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The receiver behavior at signal voltages equal to both the negative and positive supply voltage is tested.

Precondition PLT: Resistor between L+ and C/Q or C/Q and L- respectively EUT: PORT_DI (see A.4.2)

Procedure a) Set supply voltage of Master to first PSM value;Test parameter b) Apply resistor value between L+ and C/Q temporarily for at least 1 min c) Apply resistor value between C/Q and L- temporarily for at least 1 min d) Attach a Device e) Set Master to communication, for example: SMI_PortConfiguration(ABPS_NO_TYPE_CHECK) (see A.4.6) f) Get Master state, for example: SMI_PortStatus;returns ArgBlock "PortStatusList" g) Evaluation 1) h) Repeat from b) with next PSM value

Test parameter PSM = {PSMmin, PSMmax} (according to user manual) Resistor = {1 Ohm}

Post condition − 460

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Argblock

Test passed For all PSM values: PortStatusList.PortstatusInfo = OPERATE

Test not passed (examples) For any PSM value: State OPERATE not reached

Report Communication established: <yes/no> <ok | nok>

461

462

### Test Specification © IO-Link – 55 – Version V1.1.5

### 463 6.2.10 Electrical isolation of Master Port class B

### 464 Table 16 defines the test conditions for this test case.

### 465 Table 16 – Electrical isolation of Master Port class B

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0310

Name TCM_PHYL_INTF_MRGI

Purpose (short) Electrical isolation of Master Port class B power lines

Equipment under test (EUT) Master with Port class B

Test case version 1.0

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.4.2

Configuration / setup See Figure A.1 466

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test of electrical isolation between Power 1 and Power 2 for a Class B Master Port measured with isolation test voltages.

### Precondition PLT: See Figure A.1

EUT: PORT_DI (see A.4.2)

Procedure a) Set PSM to first value;Test parameter b) Set Vaux to first value;Test parameter c) Measure IgiM0 and voltage VL+ at L+ d) Evaluation 1) e) Set PSM to second value;Test parameter f) Set Vaux to second value;Test parameter g) Measure IgiM1 h) Evaluation 2)

Test parameter PSM = {PSMmax, 0 V} according to user manual Vaux = {0 V, 30 V}

Post condition − 467

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check RIsoM0 = VL+/IgiM0

2. Check RIsoM1 = 30 V/IgiM1

Test passed RIsoM0 > 2 MOhm, and RIsoM1 > 2 MOhm

Test not passed (examples) Any evaluation failed

Report RIsoM0: <value> <ok | nok> RIsoM1: <value> <ok | nok>

468

### Version V1.1.5 – 56 – Test Specification © IO-Link

### 469 6.3 Static parameters of the Device interface

### 470 6.3.1 Power supply current consumption of the Device I

### 471 Table 17 defines the test conditions for this test case.

### 472 Table 17 – Power supply current consumption of the Device I

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0011

Name TCD_PHYL_INTF_ISD

Purpose (short) Static power supply current consumption

Equipment under test (EUT) Device

Test case version 1.3

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.2.3, Table 6

Configuration / setup The supply current at Device L+ is measured 473

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Measurement of the static power supply current at the L+ of the Device

Precondition PLT: Voltage source with value VSD between L+ and L- EUT: Device set to operation mode with maximum power consumption. Maximum current consumption ISDman according to Device manual is provided.

Procedure a) Apply first value of supply voltage VSD to the Device;Test parameter b) Measure current at L+ c) Evaluation 1) d) Set Device to OPERATE with MinCycleTime e) Measure current at L+ in OPERATE f) Evaluation 2) g) Repeat from b) with next VSD value

Test parameter VSD = {18V, 30V}

Post condition Memorize measured currents 474

TEST CASE RESULTS CHECK / REACTION

Evaluation For all VSD values:

1. Check value of ISDSIOmax

2. Check value of ISDIOLmax

Test passed ISDSIOmax ≤ 200 mA (Warning if > 200 mA, but ≤ ISDman), and ISDSIOmax ≤ ISDman, and ISDIOLmax ≤ 200 mA (Warning if > 200 mA, but ≤ ISDman), and ISDIOLmax ≤ ISDman

Test not passed (examples) ISDSIOmax > ISDman, or ISDIOLmax > ISDman

Report ISDSIOmax (VSD = 18 V): <value/ n/a> <ok | nok | warning> ISDSIOmax (VSD = 30 V): <value/ n/a> <ok | nok | warning> ISDIOLmax (VSD = 18 V): <value> <ok | nok | warning> ISDIOLmax (VSD = 30 V): <value> <ok | nok | warning>

475

476

### Test Specification © IO-Link – 57 – Version V1.1.5

### 477 6.3.2 Power supply current consumption of the Device II

### 478 Table 18 defines the test conditions for this test case.

### 479 Table 18 – Power supply current consumption of the Device II

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0320

Name TCD_PHYL_INTF_DOCISD

Purpose (short) Power supply current consumption above recommendation

Equipment under test (EUT) Device, supply current ISD > 200mA (see TC_0011 report: Table 17)

Test case version 1.0

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.2.3, 5.4.1, Table 6

Configuration / setup – 480

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Verification in Device datasheet or user manual for increased power supply current requirement (> 200 mA) of the Device.

Precondition PLT: not required EUT: Device datasheet or user manual (maximum current consumption ISDman)

Procedure a) Check Device documentation for hint on constraints or user responsibility due to increased power supply current requirement b) Input: Documentation available / not available c) Evaluation 1)

Test parameter –

Post condition – 481

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Retrieve information on maximum current consumption

Test passed Information available

Test not passed (examples) Information insufficient or not available in documentation for user

Report Type of information: <value> <ok | nok | warning>

482 483

484

### Version V1.1.5 – 58 – Test Specification © IO-Link

### 485 6.3.3 Power-on behavior of the Device

### 486 Table 19 defines the test conditions for this test case.

### 487 Table 19 – Power-on behavior of the Device

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0012

Name TCD_PHYL_INTF_ISIRD

Purpose (short) Power-on current consumption (charge) and Device behavior

Equipment under test (EUT) Device with maximum supply current ISD ≤ 200 mA

Test case version 1.2

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.2.3, Table 6 and 5.4.1, Table 10

Configuration / setup The Device is powered by a Master with current supply capabilities according to ISIRM and ISM. See Figure A.4. 488

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The current and communication of the Device is monitored upon power-on: The Device charge requirements and behavior at power-on are verified at minimum and maximum power supply conditions.

Precondition PLT: Voltage source with value VSD and current monitor for ISD between L+ and L- EUT: Device is powered off

Procedure a) Switch on Device with first value of supply voltage VSD;Test parameter b) Trigger ISD measurement, if VSDmin (18V) is reached (tSTART) c) Stop ISD measurement at time tSTART + TRDL d) Evaluation 1) e) Set Device to OPERATE f) Count attempts in STARTUP g) Evaluation 2) h) Switch off Device (de-energize) i) Switch on Device with next value of supply voltage VSD;Test parameter j) Repeat from b) with next VSD value

Test parameter VSD = {18 V, 30 V}

Post condition – 489

TEST CASE RESULTS CHECK / REACTION

Evaluation For all VSD values:

1. Evaluate charge QISD using the formula 𝑄𝐼𝑆𝐷 = ∫ 𝐼𝑆𝐷(𝑡)𝑑𝑡

𝑡𝑆𝑇𝐴𝑅𝑇+𝑇𝑅𝐷𝐿 𝑡𝑆𝑇𝐴𝑅𝑇

2. Check if communication is established and check STARTUP count

Test passed QISD ≤ 70mAs, and OPERATE established and STARTUP count = 1

Test not passed (examples) Charge QISD > 70 mAs, or OPERATE not established, or STARTUP count > 1

Report QISD (VSD = 18 V): <value> <ok | nok> STARTUP count (VSD = 18 V): <value> <ok | nok> QISD (VSD = 30 V): <value> <ok | nok> STARTUP count (VSD = 30 V): <value> <ok | nok>

490

491

### Test Specification © IO-Link – 59 – Version V1.1.5

### 492 6.3.4 High-side residual voltage at Device C/Q

### 493 Table 20 defines the test conditions for this test case. This test case is only applicable for

### 494 Devices with SIO mode or Devices where the output driver can be stimulated as if in SIO mode

### 495 (e.g. via ASICs).

### 496 Table 20 – High-side residual voltage at Device C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0013

Name TCD_PHYL_INTF_VRESHIGH

Purpose (short) Static high-side driver capability

Equipment under test (EUT) Device where C/Q can be configured as output with static high level

Test case version 1.3

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.2.4, Table 7

Configuration / setup The high-side output level of the Device C/Q output is measured according to Figure A.3 497

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Driver capability of the Device high-side driver. Measurement of the voltage drop between supply L+ and C/Q output under load condition of 50 mA.

Precondition PLT: Voltage source with value VSD between L+ and L- EUT: Device is powered off

Procedure a) Apply first supply voltage VSD to the Device;Test parameter b) Apply current sink/source with 50 mA to C/Q c) Measure voltage VCQ between L+ and C/Q d) Evaluation 1) e) Repeat from b) with next VSD value

Test parameter VSD = {18 V, 30 V}

Post condition – 498

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check VCQ

Test passed For all VSD values: VCQ ≤ 3,0 V

Test not passed (examples) For any VSD value: VCQ > 3,0 V

Report VCQ (VSD = 18 V): <value> <ok | nok> VCQ (VSD = 30 V): <value> <ok | nok>

499

500

### Version V1.1.5 – 60 – Test Specification © IO-Link

### 501 6.3.5 Low-side residual voltage at Device C/Q

### 502 Table 21 defines the test conditions for this test case. This test case is only applicable for

### 503 Devices with SIO mode or Devices where the output driver can be stimulated as if in SIO mode

### 504 (e.g. via ASICs).

### 505 Table 21 – Low-side residual voltage at Device C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0014

Name TCD_PHYL_INTF_VRESLOW

Purpose (short) Static low-side driver capability

Equipment under test (EUT) Device where C/Q can be configured as output with static low level

Test case version 1.3

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.2.4, Table 7

Configuration / setup The output level of the Device C/Q output is measured according to Figure A.3 506

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Driver capability of the Device low-side driver. Measurement of the voltage drop between negative supply L- and C/Q output at sink current of 50 mA

Precondition PLT: Voltage source with value VSD between L+ and L- EUT: Device is powered off

Procedure a) Apply first supply voltage VSD to the Device;Test parameter b) Apply current sink/source with 50 mA to C/Q c) Measure voltage VCQ between C/Q and Ld) Evaluation 1) e) Repeat from b) with next VSD value

Test parameter VSD = {18 V, 30 V}

Post condition – 507

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check VCQ

Test passed For all VSD values: VCQ ≤ 3,0 V

Test not passed (examples) For any VSD value: VCQ > 3,0 V

Report VCQ (VSD = 18 V): <value> <ok | nok> VCQ (VSD = 30 V): <value> <ok | nok>

508

509

### Test Specification © IO-Link – 61 – Version V1.1.5

### 510 6.3.6 Pull-down or quiescent current at Device C/Q

### 511 Table 22 defines the test conditions for this test case.

### 512 Table 22 – Pull-down or quiescent current at Device C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0015

Name TCD_PHYL_INTF_IQQD

Purpose (short) Static quiescent current

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.2.4, Table 7

Configuration / setup Measurement of current at C/Q in receive mode 513

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Measurement of the quiescent current into C/Q in receive mode

Precondition PLT: Voltage source with value VSD between L+ and L- and stimulus voltage source VID between C/Q and L-. EUT: Device C/Q is set to a receive mode

Procedure a) Apply first value of supply voltage VSD to the Device;Test parameter b) Apply voltage VID with value of input threshold high voltage (VTHHD = 13 V) c) Measure current ICQ at C/Q d) Evaluation 1) e) Apply voltage VID with value of positive supply voltage VSD f) Measure current ICQ at C/Q g) Evaluation 2) h) Repeat from b) with next VSD value

Test parameter VSD = {18 V, 30 V}

Post condition – 514

TEST CASE RESULTS CHECK / REACTION

Evaluation For all VSD values:

1. Check value 1 of ICQ

2. Check value 2 of ICQ

Test passed For all VSD values: Value 1 and value 2 ≤ 15 mA

Test not passed (examples) For all VSD values: Value 1 or value 2 > 15 mA

Report ICQ (VSD = 18 V, VID = 13V): <value> <ok | nok> ICQ (VSD = 18 V, VID = VSD): <value> <ok | nok> ICQ (VSD = 30 V, VID = 13V): <value> <ok | nok> ICQ (VSD = 30 V, VID = VSD): <value> <ok | nok>

515

516

### Version V1.1.5 – 62 – Test Specification © IO-Link

### 517 6.3.7 High-level input threshold voltage at Device C/Q

### 518 Table 23 defines the test conditions for this test case.

### 519 Table 23 – High-level input threshold voltage at Device C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0016

Name TCD_PHYL_INTF_VTHHD

Purpose (short) Statically input high-level threshold at C/Q

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.2.2, Table 5

Configuration / setup See Figure A.10 520

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Measurement of the threshold voltage for high-level at the C/Q

Precondition PLT: Voltage source with value VSD between L+ and L- and a stimulus voltage VID between C/Q and L- EUT: Device set to a PL test set-up, where a monitor signal of internal UART is available. This test set-up assumes C/Q port is in receive mode and change of the state at the monitor signal is observed. Otherwise, appropriate information.

Procedure a) If PL test set-up is available perform steps b) to f) b) Apply first value of supply voltage VSD to the Device;Test parameter c) Sweep stimulus voltage VID from 5 V to 15 V in steps of maximum 0,1 V d) Capture the value of VID on the state change of monitor signal e) Evaluation 1) f) Repeat from c) with next VSD value g) If PL test set-up is not available perform steps h) to i) h) Provide VID manually as input from ASIC data sheet or earlier measurements i) Evaluation 1)

Test parameter VSD = {18 V, 30 V}

Post condition Memorize value VID (for all VSD) 521

TEST CASE RESULTS CHECK / REACTION

Evaluation For all measured values:

1. Check VID

Test passed VID ≥ 10.5V, and VID ≤ 13.0 V

Test not passed (examples) VID < 10.5V, or VID > 13.0 V

Report VID @ Transition 0→1 (VSD = 18 V): <value> <ok | nok> VID @ Transition 0→1 (VSD = 30 V): <value> <ok | nok>

522

523

### Test Specification © IO-Link – 63 – Version V1.1.5

### 524 6.3.8 Low-level input threshold voltage at Device C/Q

### 525 Table 24 defines the test conditions for this test case.

### 526 Table 24 – Low-level input threshold voltage at Device C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0017

Name TCD_PHYL_INTF_VTHLD

Purpose (short) Static input low-level threshold at C/Q

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.2.2, Table 5

Configuration / setup See Figure A.10 527

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Measurement of the threshold voltage for low-level at the C/Q

Precondition PLT: Voltage source with value VSD between L+ and L- and a stimulus voltage VID between C/Q and L- EUT: Device set to a PL test set-up, where a monitor signal of internal UART is available. This test set-up assumes C/Q port is in receive mode and change of the state at the monitor signal is observed. Otherwise, appropriate information.

Procedure a) If PL test set-up is available perform steps b) to f) b) Apply first value of supply voltage VSD to the Device;Test parameter c) Sweep stimulus voltage VID from 15 V to 5 V in steps of maximum 0,1 V d) Capture the value of VID on the state change of monitor signal e) Evaluation 1) f) Repeat from c) with next VSD value g) If PL test set-up is not available perform steps h) to i) h) Provide VID manually as input from ASIC data sheet or earlier measurements i) Evaluation 1)

Test parameter VSD = {18 V, 30 V}

Post condition Memorize value VID (for all VSD) 528

TEST CASE RESULTS CHECK / REACTION

Evaluation For all measured values:

1. Check VID

Test passed VID ≥ 8,0 V, AND VID ≤ 11,5 V

Test not passed (examples) VID < 8,0V, OR VID > 11,5 V

Report VID @ Transition 1→0 (VSD = 18 V): <value> <ok | nok> VID @ Transition 1→0 (VSD = 30 V): <value> <ok | nok>

529

530

### Version V1.1.5 – 64 – Test Specification © IO-Link

### 531 6.3.9 Input hysteresis voltage at Device C/Q

### 532 Table 25 defines the test conditions for this test case.

### 533 Table 25 – Input hysteresis voltage at Device C/Q

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0018

Name TCD_PHYL_INTF_VHYSD

Purpose (short) Calculation of input hysteresis at C/Q

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.2.2, Table 5

Configuration / setup See TC_0016 and TC_0017 534

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Calculation of the hysteresis voltage at C/Q based on VTHHD and VTHLD

Precondition Value VID(TC_0016) from TC_0016 is available for all VSD values Value VID(TC_0017) from TC_0017 is available for all VSD values

Procedure –

Test parameter –

Post condition – 535

TEST CASE RESULTS CHECK / REACTION

Evaluation For all VSD values: VHYSD = Value VID(TC_0016) – Value VID(TC_0017)

Test passed For all VSD values: VHYSD ≥ 0 V

Test not passed (examples) For any VSD value: VHYSD < 0 V

Report VHYSD (VSD = 18 V): <value> <ok | nok> VHYSD (VSD = 30 V): <value> <ok | nok>

536

537

538

539

### Test Specification © IO-Link – 65 – Version V1.1.5

### 540 6.3.10 Permissible voltage range at Device C/Q

### 541 Table 26 defines the test conditions for this test case.

### 542 Table 26 – Permissible voltage range at Device C/

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0300

Name TCD_PHYL_INTF_VOLTRANGECQ

Purpose (short) Permissible voltage range at Device C/Q

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.2.2, Table 5, VIL and VIH

Configuration / setup See Figure A.10 with modifications (see Precondition) 543

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Device behavior is tested after exposures to signal voltages exceeding the supply voltage

Precondition PLT: Voltage source with value VSD between L+ and L- and a stimulus voltage VID between C/Q and L- via a series resistance of 1 Ohm, and a Master compliant to [1] EUT: Device is attached to PLT

Procedure a) Apply supply voltage (VSD = 29 V) to Device

b) Start SDCI communication

c) Stop SDCI communication (disconnect C/Q from Master port, EUT C/Q is still in receive mode)

d) Apply first value of stimulus voltage VID for at least 1 min ;Test parameter

e) Apply next value of stimulus voltage VID for at least 1 min ;Test parameter

f) Remove stimulus voltage VID

g) Reconnect C/Q to Master port

h) Start SDCI communication

i) Evaluation 1)

Test parameter VID = {(VSD + 1 V), -1 V}

Post condition – 544

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check communication

Test passed Device established communication at least in 5 s and no errors.

Test not passed (examples) Communication not established in 5 s , or Errors occurred

Report Communication established: <yes/no> <ok | nok>

545 546

### Version V1.1.5 – 66 – Test Specification © IO-Link

### 547 6.3.11 Electrical isolation of Device power supplies

### 548 Table 27 defines the test conditions for this test case.

### 549 Table 27 – Electrical isolation of Device power supplies

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0311

Name TCD_PHYL_INTF_DRGI

Purpose (short) Electrical isolation of Device with Master Port class B requirements

Equipment under test (EUT) Device using Power 1 and Power 2

Test case version 1.1

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.4.2

Configuration / setup See Figure A.2 550

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test of electrical isolation between Power 1 and Power 2 of a Device with Master Port Class B requirements

Precondition PLT: See Figure A.2 EUT: Device is operational

Procedure a) Apply supply VSD to Device with VSDmax (VSD = 30 V) b) Apply first value of Vaux at auxiliary power supply;Test parameter c) Measure current IgiD0 d) Evaluation 1) e) Apply next value of Vaux at auxiliary power supply;Test parameter f) Measure current IgiD1 g) Evaluation 2)

Test parameter Vaux = {0 V, 30 V}

Post condition – 551

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check RIsoD0 = VSDmax/IgiD0

2. Check RIsoD1 = VSDmax/IgiD1

Test passed RIsoD0 > 2 MOhm, and RIsoD1 > 2 MOhm

Test not passed (examples) Any evaluation failed

Report RIsoD0: <value> <ok | nok> RIsoD1: <value> <ok | nok>

552

553

### Test Specification © IO-Link – 67 – Version V1.1.5

### 554 6.4 Wake-Up generation of the Master interface

### 555 6.4.1 Wake-Up current pulse high

### 556 Table 28 defines the test conditions for this test case.

### 557 Table 28 – Wake-Up current pulse high

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0021

Name TCM_PHYL_INTF_IQWUH

Purpose (short) Driver capability of the wake-up pulse – high-side driver

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.3.3, Table 8

Configuration / setup See Figure A.5 558

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The minimum requirement for the Master output peak current IQPKHM is verified with a resistive load at Master C/Q. A voltage VIM above the threshold VTHHmax during a wakeup pulse indicates that the minimum requirement is met.

Precondition PLT: See Figure A.5 EUT: PORT_AUTOSTART (see A.4.2)

Procedure a) Apply first supply voltage PSM to Master;Test parameter b) Measure voltage VIM during Wake-up pulse c) Evaluation 1) d) Repeat from b) with next PSM value

Test parameter PSM = {PSMmin, PSMmax} (according to user manual); Rload = {L+(PSMmin) / IQPKHMmin, L+(PSMmax) / IQPKHMmin}

Post condition – 559

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check voltage VIM

Test passed For all PSM: VIM ≥ VTHHmax

Test not passed (examples) For any PSM: VIM < VTHHmax

Report VIM @ WURQ (PSMmin): <value> <ok | nok> VIM @ WURQ (PSMmax): <value> <ok | nok>

560

561

### Version V1.1.5 – 68 – Test Specification © IO-Link

### 562 6.4.2 Wake-Up pulse duration high

### 563 Table 29 defines the test conditions for this test case.

### 564 Table 29 – Wake-Up pulse duration high

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0022

Name TCM_PHYL_INTF_TWUH

Purpose (short) Wake-Up pulse duration (high pulse)

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.3.3 Table 8

Configuration / setup See Figure A.7 565

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Verify the requirements for the wake-up pulse duration (high pulse) at Master C/Q under maximum resistive load conditions. The minimum and maximum pulse duration is measured with evaluations at the extreme values of the thresholds VTHL and VTHH.

Precondition PLT: Resistive load (Rload) between C/Q and L-; voltage measurement between C/Q and L- EUT: PORT_AUTOSTART (see A.4.2)

Procedure a) Apply first supply voltage PSM to Master and Rload;Test parameter b) Trigger time measurement t1 on voltage level at C/Q ≥ VTHHmin c) Trigger time measurement t2 on voltage level at C/Q ≤ VTHLmin d) Evaluation 1) e) Trigger time measurement t3 on voltage level at C/Q ≥ VTHHmax f) Trigger time measurement t4 on voltage level at C/Q ≤ VTHLmax g) Evaluation 2) h) Repeat from b) with next PSM value

Test parameter PSM = {PSMmin, PSMmax} (according to user manual); Rload = {L+(PSMmin) / IQPKHMmin, L+(PSMmax) / IQPKHMmin}

Post condition – 566

TEST CASE RESULTS CHECK / REACTION

Evaluation For all PSM values (see Figure A.17):

1. Determine tWUmax = t2 – t1

2. Determine tWUmin = t4 – t3

Test passed 75 μs ≤ tWUmin, and tWUmax ≤ 85 μs

Test not passed (examples) tWUmax > 85 μs, or tWUmin < 75 μs

Report tWUmin @ WURQ: <value> <ok | nok> tWUmax @ WURQ: <value> <ok | nok>

567

568

### Test Specification © IO-Link – 69 – Version V1.1.5

### 569 6.4.3 Wake-Up current pulse low

### 570 Table 30defines the test conditions for this test case.

### 571 Table 30 – Wake-Up current pulse low

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0023

Name TCM_PHYL_INTF_IQWUL

Purpose (short) Drive capability of the wake-up pulse – low-side driver

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.3.3 Table 8

Configuration / setup See Figure A.8 572

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The minimum requirement for the Master output peak current IQPKLM is verified with a resistive load at Master C/Q. A voltage at C/Q below the threshold low level of a receiver VTHLmin during a wake-up pulse indicates that the minimum requirement is met.

Precondition PLT: Resistive load (Rload) between L+ and C/Q; voltage measurement between C/Q and L- EUT: PORT_AUTOSTART (see A.4.2)

Procedure a) Apply first supply voltage PSM to the Master and first Rload;Test parameter b) Measure voltage VIM during Wake-up pulse c) Evaluation 1) d) Repeat from b) with next PSM value and next Rload value

Test parameter PSM = {PSMmin, PSMmax} (according to user manual); Rload = {(VL+min - 8 V)/0,5 A, (VL+max - 8 V)/0,5 A}

Post condition – 573

TEST CASE RESULTS CHECK / REACTION

Evaluation For all PSM values:

1. Check voltage VIM

Test passed For all PSM values: VIM ≤ VTHLmin

Test not passed (examples) For any PSM value: VIM > VTHLmin

Report VIM @ WURQ (PSMmin): <value> <ok | nok> VIM @ WURQ (PSMmax): <value> <ok | nok>

574

575

### Version V1.1.5 – 70 – Test Specification © IO-Link

### 576 6.4.4 Wake-Up pulse duration low

### 577 Table 31 defines the test conditions for this test case.

### 578 Table 31 – Wake-Up pulse duration low

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0024

Name TCM_PHYL_INTF_TWUL

Purpose (short) Wake-Up pulse duration (low pulse)

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master PL test: test to pass

Specification (clause) [1], see 5.3.3.3, Table 8

Configuration / setup See Figure A.8 579

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Verify the requirements for the wake-up pulse duration (low pulse) at Master C/Q under maximum resistive load conditions. The minimum and maximum pulse duration is measured with evaluations at the extreme values of thresholds VTHL and VTHH

Precondition PLT: Resistive load (Rload) between L+ and C/Q; voltage measurement between C/Q and L- EUT: PORT_AUTOSTART (see A.4.2)

Procedure a) Apply first supply voltage PSM to Master and first Rload;Test parameter b) Trigger time measurement t1 on voltage level at C/Q ≤ VTHLmax c) Trigger time measurement t2 on voltage level at C/Q ≥ VTHHmax d) Evaluation 1) e) Trigger time measurement t3 on voltage level at C/Q ≤ VTHLmin f) Trigger time measurement t4 on voltage level at C/Q ≥ VTHHmin g) Evaluation 2) h) Repeat from b) with next PSM value and next Rload value

Test parameter PSM = {PSMmin, PSMmax} (according to user manual); Rload = {(VL+min - 8 V)/0,5 A, (VL+max - 8 V)/0,5 A}

Post condition – 580

TEST CASE RESULTS CHECK / REACTION

Evaluation For all PSM values (see Figure A.18):

1. Determine tWUmax = t2 – t1

2. Determine tWUmin = t4 – t3

Test passed 75 μs ≤ tWUmin, and tWUmax ≤ 85 μs

Test not passed (examples) tWUmax > 85 μs, or tWUmin < 75 μs

Report tWUmin @ WURQ: <value> <ok | nok> tWUmax @ WURQ: <value> <ok | nok>

581

582

### Test Specification © IO-Link – 71 – Version V1.1.5

### 583 6.5 Wake-Up detection of the Device interface

### 584 6.5.1 Wake-Up pulse detection high

### 585 Table 32 defines the test conditions for this test case.

### 586 Table 32 – Wake-Up pulse detection high

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0025

Name TCD_PHYL_INTF_TWUH

Purpose (short) Wake-Up pulse detection capability (high pulse)

Equipment under test (EUT) Device with SIO mode, C/Q with signal low

Test case version 1.2

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.3.3, Table 8

Configuration / setup Signal generator connected to EUT according Figure A.11 587

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) A wake-up pulse is applied to Device C/Q with timing at specified limits. The wakeup pulse is indicated via a test signal/indicator on the Device side. The Device reaction is evaluated. This test is only applicable for Devices that can drive C/Q in SIO mode to low and where means to monitor the reaction on the WURQ are provided (e.g. Interrupt line of IO-Link PHY).

Precondition PLT: Signal Generator with high impedance or low output EUT: SIO mode. Device set to a PL test set-up, where a monitor signal of the wake-up detection is available, e.g. interrupt line of PHY. Otherwise appropriate information is available.

Procedure a) If PL test set-up is available, perform b) to j) b) Set L+ Voltage VSD of the Device to first value;Test parameter c) Configure EUT to drive output in SIO mode to signal low d) Apply current pulse with IQWU = IQPKHmin (current source) and of TWUmin (75 μs) to C/Q. e) Monitor test signal/WURQ indication at Device f) Evaluation 1) g) Apply current pulse with IQWU = IQPKHmin (current source) and of TWUmax (85 μs) to C/Q. h) Monitor test signal/WURQ indication at Device i) Evaluation 1) j) Repeat test from c) with next VSD voltage k) If PL test set-up is not available, proof of correct pulse detection is provided by manufacturer

Test parameter VSD = {18V, 30V}

Post condition – 588

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check monitored test signal for WURQ indication

Test passed WURQ indicated, or Proof of correct pulse detection is provided by manufacturer

Test not passed (examples) WURQ not indicated, or No proof

Report WURQ (VSD = 18 V): <yes/no> <ok | nok> WURQ (VSD = 30 V): <yes/no> <ok | nok> Proof of correct pulse detection: <yes/no> <ok | nok>

589

590

### Version V1.1.5 – 72 – Test Specification © IO-Link

### 591 6.5.2 Wake-Up pulse detection low

### 592 Table 33 defines the test conditions for this test case.

### 593 Table 33 – Wake-Up pulse detection low

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0026

Name TCD_PHYL_INTF_TWUL

Purpose (short) Wake-Up pulse detection capability (low pulse)

Equipment under test (EUT) Device with SIO mode, C/Q with signal high

Test case version 1.2

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.3.3, Table 8

Configuration / setup Signal generator connected to EUT according Figure A.11 594

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) A wake-up pulse is applied at the Device C/Q port with timing at specified limits. The wake-up request “WURQ” is indicated via a test signal / indicator. The Device reaction is evaluated. This test is only applicable for Devices that can drive C/Q in SIO mode to high and where means to monitor the reaction on the WURQ are provided (e.g. Interrupt line of IO-Link PHY).

Precondition PLT: Signal Generator with high impedance or high output EUT: SIO mode. Device set to a PL test set-up, where a monitor signal of the wake-up detection is available, e.g. interrupt line of PHY. Otherwise appropriate information is available.

Procedure a) If PL test set-up is available, perform b) to j) b) Set L+ Voltage VSD to Device to first value;Test parameter c) Configure EUT to drive output in SIO mode to signal high d) Apply current pulse with IQWU = IQPKHmin (current source) and of TWUmin (75 μs) to C/Q. e) Monitor test signal / WURQ indication at Device f) Evaluation 1) g) Apply current pulse with IQWU = IQPKHmin (current source) and of TWUmax (85 μs) to C/Q. h) Monitor test signal / WURQ indication at Device i) Evaluation 1) j) Repeat test from c) with next VSD voltage k) If PL test set-up is not available, proof of correct pulse detection is provided by manufacturer

Test parameter VSD = {18V, 30V}

Post condition – 595

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check monitored test signal for WURQ indication

Test passed WURQ indicated, or Proof of correct pulse detection is provided by manufacturer

Test not passed (examples) WURQ not indicated, or No proof

Report WURQ (VSD = 18 V): <yes/no> <ok | nok> WURQ (VSD = 30 V): <yes/no> <ok | nok> Proof of correct pulse detection: <yes/no> <ok | nok>

596

597

### Test Specification © IO-Link – 73 – Version V1.1.5

### 598 6.5.3 Wake-Up receive enable delay (C/Q high)

### 599 Table 34 defines the test conditions for this test case.

### 600 Table 34 – Wake-Up receive enable delay (C/Q high)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0027

Name TCD_PHYL_INTF_TRENHIGH

Purpose (short) Determine Receive Enable Delay after Wake-up Request

Equipment under test (EUT) Device with SIO mode, C/Q signal high

Test case version 1.3

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.3.3, Table 10

Configuration / setup See Figure A.12 601

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Device shall release the high-side output driver after successful reception of a wake-up request. Measure wake-up receive enable delay of the Device with high signal at C/Q. The delay time is measured with a resistive voltage divider applied between L+ to C/Q and C/Q to L.

Precondition PLT: Signal generator with high impedance or high output EUT: SIO Mode (output signal high)

Procedure a) Stimulate Device for SIO mode output signal = high b) Apply wake-up request pulse (negative pulse) and return to high impedance c) Monitor C/Q signal starting from negative edge of wake-up request pulse for a duration of TRENmax d) Evaluation 1)

Test parameter –

Post condition – 602

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check tREN = time from start of wake-up request until level of C/Q reaches VTHLmin

Test passed tREN ≤ 500 μs

Test not passed (examples) VTHLmin not reached

Report tREN @ C/Q = high: <value> <ok | nok>

603

604

### Version V1.1.5 – 74 – Test Specification © IO-Link

### 605 6.5.4 Wake-Up receive enable delay (C/Q low)

### 606 Table 35 defines the test conditions for this test case.

### 607 Table 35 – Wake-Up receive enable delay (C/Q low)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0028

Name TCD_PHYL_INTF_TRENLOW

Purpose (short) Determine Receive Enable Delay after Wake-up Request

Equipment under test (EUT) Device with SIO mode, C/Q signal low

Test case version 1.3

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3.3.3, Table 10

Configuration / setup See Figure A.13 608

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Device shall release the low-side output driver after successful reception of a wake-up request. Measure wake-up receive enable delay of the Device with C/Q low. The delay time is measured with a resistive voltage divider applied between L+ to C/Q and C/Q to L.

Precondition PLT: Signal Generator with high impedance or low output EUT: SIO mode (output signal low)

Procedure a) Stimulate Device for SIO mode output signal = low b) Apply wake-up request pulse (positive pulse) and return to high impedance c) Monitor C/Q signal starting from positive edge of wake-up request pulse for a duration of TRENmax d) Evaluation 1)

Test parameter –

Post condition – 609

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check tREN = time from start of wake-up request until level of C/Q reaches VTHHmax

Test passed tREN ≤ 500 μs

Test not passed (examples) VTHHmax not reached

Report tREN @ C/Q = low: <value> <ok | nok>

610

611

### Test Specification © IO-Link – 75 – Version V1.1.5

### 612 6.5.5 SDCI readiness delay

### 613 Table 36 defines the test conditions for this test case.

### 614 Table 36 – SDCI readiness delay

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0029

Name TCD_PHYL_INTF_TRDL

Purpose (short) SDCI Receive Enable Delay after Power-On

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device PL test: test to pass

Specification (clause) [1], see 5.3, 7,3.2, Table 7, Table 9, Table 42

Configuration / setup Reference-Master and EUT with VSD and C/Q monitoring (see Figure A.14) 615

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Device shall be able to enter communication after the SDCI readiness delay. Measure SDCI readiness delay of the Device after a power-on situation.

Precondition PLT: Reference-Master, Port power = OFF EUT: unpowered

Procedure a) Port power = ON, e.g. via SMI_PortPowerOffOn(ABPS_PORTPOWERON) b) Monitor VSD voltage at L+ c) Identify time t0 when VSD reaches VSDmin = 18V d) Master starts communication with wake-up sequence at t0 + TRDLmax e) Monitor C/Q signal for more than t0 + TRDLmax + TDWU f) Evaluation 1)

Test parameter VSM = 24 V

Post condition – 616

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Device responses to Wake-up request at Device transmission rate

Test passed Device responds to first Wake-up request and communicates

Test not passed (examples) Device does not respond to first Wake-up request

Report Response to first Wake-up request @ TRDL: <yes/no> <ok | nok>

617 618

### Version V1.1.5 – 76 – Test Specification © IO-Link

### 619 6.5.6 Time to return to SIO after failed wake-up

### 620 Table 37 defines the test conditions for this test case.

### 621 Table 37 – Time to return to SIO after failed wake-up

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0301

Name TCD_PHYL_INTF_TDELAYTOSIO

Purpose (short) Time TDSIO within permitted range

Equipment under test (EUT) Device with SIO mode, C/Q signal high

Test case version 1.1

Category / type Device PL test: test to pass

Specification (clause) [1], see 7.3.2.2, Table 42

Configuration / setup Master and EUT with CQ monitoring according to Figure A.9 622

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Delay time TDSIO from an unsuccessful Wake-Up to the return of the Device to SIOmode is tested.

Precondition PLT: See Figure A.9 EUT: SIO mode. Device set to a PL test set-up, where the output of the Device can be set to high level

Procedure a) Apply negative wake-up request pulse to Device b) Monitor signal at C/Q c) Measure tDSIO from WURQ to transition to stable ’H’-level at C/Q d) Repeat another 5 times steps a) to c). Memorize all values of tDSIO e) Evaluation 1)

Test parameter VSD = 24 V

Post condition – 623

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) tDSIO of all 6 measurements shall be within specified limits. Identify minimum and maximum values from all measured values of TDSIO

Test passed For all values: 60 ms ≤ tDSIO ≤ 300 ms

Test not passed (examples) For any value: tDSIO < 60 ms, or tDSIO > 300 ms

Report tDSIOmin: <value> tDSIOmax: <value> tDSIO within specified boundaries: <yes/no> <ok | nok>

624

625

### Test Specification © IO-Link – 77 – Version V1.1.5

### 626 6.5.7 Time to Fallback after Master command

### 627 Table 38 defines the test conditions for this test case.

### 628 Table 38 – Time to Fallback after Master command

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0302

Name TCD_PHYL_INTF_TTOFALLBACK

Purpose (short) Time tFBD within permitted range

Equipment under test (EUT) Device with C/Q high in SIO mode (indicated in IODD)

Test case version 1.2

Category / type Device PL test: test to pass

Specification (clause) [1], see 7.3.2.3, Table 42

Configuration / setup Reference-Master and EUT (see A.1.5) 629

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) tFBD from a confirmed MasterCommand "Fallback" to the return of the Device to SIO-mode is tested.

Precondition Reference-Master: PORT_INACTIVE EUT: C/Q high in SIO mode

Procedure a) Set Device to OPERATE mode e.g. via SMI_PortConfiguration(ABPS_NO_TYPE_CHECK) b) Read VendorID e.g. via SMI_PortStatus c) Read MasterCycleTime from Master or EUT d) Set Device to SIO mode (apply "Fallback" MasterCommand), e.g. via SMI_PortConfiguration(ABPS_PORTINACTIVE) e) Monitor signal level at C/Q f) Measure tFBD from end of the first Device reply message to a Master Write message with MasterCommand "Fallback" and the transition to stable ’H’-level at C/Q g) Evaluation 1) h) Set Device to PREOPERATE mode e.g. via SMI_PortConfiguration (ABPS_TPYE_COMP <VendorID ≠ PortStatus.VendorID>);stimulate config error i) Set Device to SIO mode (apply "Fallback" MasterCommand), e.g. via SMI_PortConfiguration(ABPS_PORTINACTIVE) j) Monitor level at C/Q k) Measure tFBD from end of the first Device reply message to a Master Write message with MasterCommand "Fallback" and the transition to stable ’H’-level at C/Q l) Evaluation 2)

Test parameter –

Post condition – 630

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Fallback delay tFBD @ OPERATE

2. Check Fallback delay tFBD @ PREOPERATE

Test passed Three times MasterCycleTime ≤ tFBD @ OPERATE ≤ 500 ms, and Three times RecoveryTime ≤ tFBD @ PREOPERATE ≤ 500 ms

Test not passed (examples) tFBD @ OPERATE < three times MasterCycleTime, or tFBD @ OPERATE > 500 ms, or tFBD @ PREOPERATE < three times RecoveryTime, or tFBD @ PREOPERATE > 500 ms

Report tFBD @ OPERATE: <value> <ok | nok> tFBD @ PREOPERATE: <value> <ok | nok>

631

632

### Version V1.1.5 – 78 – Test Specification © IO-Link

### 633 6.6 Dynamic parameters of the Master and Device interface

### 634 6.6.1 Bit eye-diagram with maximum load (Master)

### 635 Table 39 defines the test conditions for this test case.

### 636 Table 39 – Bit eye-diagram with maximum load (Master)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0030

Name TCM_PHYL_INTF_BITEYEMAXLOAD

Purpose (short) Eye-diagram of ‘low’ and ‘high’ bits

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master signal test: test to pass

Specification (clause) [1], see 5.3.3.2, Figure 22, Table 5, Table 9

Configuration / setup EUT and Reference-Device (see Figure A.15 and clause A.1.5) 637

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The waveform shall meet the requirements of the eye diagram under maximum load conditions. Test waveform for bits at the C/Q input on the receiver side with a maximum permissible load applied. UART frames shall provide rising and falling edges at every bit position.

Precondition Reference-Device: disconnected EUT: PORT_INACTIVE

Procedure a) Attach line simulation circuit b) Attach Reference-Device with first values from COM and CQD;Test parameter c) Apply first value of PSM to Master;Test parameter d) Set Master to SDCI mode, e.g. via SMI_PortConfiguration(ABPS_NO_TYPE_CHECK) e) Wait until OPERATE is reached to check for communication, e.g. via SMI services f) Record waveforms on Device side for a minimum of 100 EUT UART frames g) Evaluation 1) h) Repeat from step d) with next value of PSM i) Repeat from step c) with next values from COM and CQD

Test parameter COM = {COM2, COM3}, CQD = {10 nF, 1 nF}, PSM = {PSMmin, PSMmax} (according to user manual)

Post condition Memorize waveforms 638

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Determine tDR, tDF, VIHDMAX, V+D, V0D, VILDMIN in waveform

Test passed For all monitored bits of the UART frames: tDR, tDF < 0,2 TBIT, and VIHDMAX – V+D < 1,0 V, and V0D – VILDMIN < 1,0 V

Test not passed (examples) Any of the conditions in test passed is not met

Report Bit eye-diagram @ maximum load (PSMmin): <yes/no> <ok | nok> Bit eye-diagram @ maximum load (PSMmax): <yes/no> <ok | nok>

639

640

### Test Specification © IO-Link – 79 – Version V1.1.5

### 641 6.6.2 Bit eye-diagram with maximum load (Device)

### 642 Table 39 defines the test conditions for this test case.

### 643 Table 40 – Bit eye-diagram with maximum load (Device)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0294

Name TCD_PHYL_INTF_BITEYEMAXLOAD

Purpose (short) Eye-diagram of ‘low’ and ‘high’ bits

Equipment under test (EUT) Device

Test case version 1.2

Category / type Device signal test: test to pass

Specification (clause) [1], see 5.3.3.2, Figure 22, Table 5, Table 9,

Configuration / setup Reference-Master connected to EUT (see Figure A.15 and clause A.1.5) 644

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The waveform shall meet the requirements of the eye diagram under maximum load conditions. Test waveform for bits at C/Q input on the receiver side with a maximum permissible load applied. UART frames shall provide rising and falling edges at every bit position.

Precondition Reference-Master: PORT_INACTIVE EUT: SIO mode

Procedure a) Attach line simulation b) Attach Reference-Master with CQM = 1 nF c) Set VSM to first value;Test parameter d) Set Master to SDCI mode, e.g. via SMI_PortConfiguration(ABPS_NO_TYPE_CHECK) e) Wait until OPERATE is reached to check for communication, e.g. via SMI services f) Record waveforms on Master side for a minimum of 100 EUT UART frames g) Evaluation 1) h) Repeat from step c) with next value of VSM

Test parameter VSM = {20V, 30V}

Post condition Memorize waveforms 645

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Determine tDR, tDF, VIHMMAX, V+M, V0M, VILMMIN in waveform

Test passed For all monitored bits of the UART frames: tDR, tDF < 0,2 TBIT, and VIHMMAX – V+M < 1,0 V, and V0M – VILMMIN < 1,0 V

Test not passed (examples) Any of the conditions in test passed is not met

Report Bit eye-diagram @ maximum load (VSM = 20 V): <yes/no> <ok | nok> Bit eye-diagram @ maximum load (VSM = 30 V): <yes/no> <ok | nok>

646

647

### Version V1.1.5 – 80 – Test Specification © IO-Link

### 648 6.6.3 Bit eye-diagram with minimum load (Master)

### 649 Table 41 defines the test conditions for this test case.

### 650 Table 41 – Bit eye-diagram with minimum load (Master)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0031

Name TCM_PHYL_INTF_BITEYEMINLOAD

Purpose (short) Eye-diagram of ‘low’ and ‘high’ bits

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master signal test: test to pass

Specification (clause) [1], see 5.3.3.2, Figure 22, Table 5, Table 9

Configuration / setup EUT connected to Reference-Device without line simulation circuit (see A.1.5) 651

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The waveform shall meet the requirements of the eye-diagram under minimum line load conditions. Test waveform for bits at the C/Q input on the receiver side with an applied minimum load. UART frames shall provide rising and falling edges at every bit position.

Precondition Reference-Device: disconnected EUT: PORT_INACTIVE

Procedure a) Attach Reference-Device with CQD = 500 pF and set first value of COM;Test parameter b) Apply first value of PSM to Master;Test parameter c) Set Master to communication, e.g. via SMI_PortConfiguration (ABPS_NO_TYPE_CHECK) d) Wait until OPERATE is reached to check for communication, e.g. via SMI services e) Record bit-waveforms on the Device side for a minimum of 100 EUT UART frames f) Evaluation 1) g) Repeat from step c) with next value of PSM h) Repeat from step b) with next value of COM

Test parameter COM= {COM2, COM3} PSM = {PSMmin, PSMmax} (according to user manual)

Post condition Memorize waveforms 652

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Determine tDR, tDF, VIHDMAX, V+D, V0D, VILDMIN in waveform

Test passed For all monitored bits of the UART frames: tDR, tDF < 0,2 TBIT, and VIHDMAX – V+D < 1,0 V, and V0D – VILDMIN < 1,0 V

Test not passed (examples) Any of the conditions in test passed is not met

Report Bit eye-diagram @ minimum load (PSMmin): <yes/no> <ok | nok> Bit eye-diagram @ minimum load (PSMmax): <yes/no> <ok | nok>

653

654

### Test Specification © IO-Link – 81 – Version V1.1.5

### 655 6.6.4 Bit eye-diagram with minimum load (Device)

### 656 Table 41 defines the test conditions for this test case.

### 657 Table 42 – Bit eye-diagram with minimum load (Device)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0295

Name TCD_PHYL_INTF_BITEYEMINLOAD

Purpose (short) Eye-diagram of ‘low’ and ‘high’ bits

Equipment under test (EUT) Device

Test case version 1.2

Category / type Device signal test: test to pass

Specification (clause) [1], see 5.3.3.2, Figure 22, Table 5, Table 9

Configuration / setup Reference-Master connected to EUT without line simulation (see A.1.5) 658

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The waveform shall meet the requirements of the eye-diagram under minimum line load conditions. Test waveform for bits at C/Q input on the receiver side with an applied minimum line load. UART frames shall provide rising and falling edges at every bit position.

Precondition Reference-Master: PORT_INACTIVE EUT: SIO mode

Procedure a) Attach Reference-Master with CQM = 500pF b) Set VSM to first value;Test parameter c) Set Reference-Master to SDCI mode, e.g. via SMI_PortConfiguration(ABPS_NO_TYPE_CHECK) d) Wait until OPERATE to check for communication, e.g. via SMI services e) Record waveforms on the Master side for a minimum of 100 EUT UART frames f) Evaluation 1) g) Repeat from step c) with next value of VSM

Test parameter VSM = {20V, 30V}

Post condition Memorize waveforms 659

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Determine tDR, tDF, VIHMMAX, V+M, V0M, VILMMIN in waveform

Test passed For all monitored bits of the UART frames: tDR, tDF < 0,2 TBIT, and VIHMMAX – V+M < 1,0 V, and V0M – VILMMIN < 1,0 V

Test not passed (examples) Any of the conditions in test passed is not met

Report Bit eye-diagram @ minimum load (VSM = 20 V): <yes/no> <ok | nok> Bit eye-diagram @ minimum load (VSM = 30 V): <yes/no> <ok | nok>

660

661

### Version V1.1.5 – 82 – Test Specification © IO-Link

### 662 6.6.5 UART frame eye-diagram with maximum load (Master)

### 663 Table 43 defines the test conditions for this test case.

### 664 Table 43 – UART frame eye-diagram with maximum load (Master)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0032

Name TCM_PHYL_INTF_UARTEYEMAXLOAD

Purpose (short) Eye-diagram of the UART frame

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master signal test: test to pass

Specification (clause) [1], see 5.3.3.2, Figure 23, Table 9

Configuration / setup EUT and Reference-Device using line simulation (see Figure A.15 and A.1.5) 665

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The waveform shall meet the requirements of the eye-diagram under maximum line load conditions. Test waveform for UART frame at the C/Q input on the receiver side with an applied maximum permissible load.

Precondition Memorized bit waveforms from TC_0030

Procedure –

Test parameter –

Post condition – 666

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Apply mask with keep-out areas according to [1], Figure 23 to recorded UART frame waveforms of TC_0030

Test passed UART frame waveforms do not enter keep-out areas

Test not passed (examples) Any UART frame waveform does enter keep-out areas

Report UART frame eye-diagram @ maximum load (PSMmin): <yes/no> <ok | nok> UART frame eye-diagram @ maximum load (PSMmax): <yes/no> <ok | nok>

667

668

### Test Specification © IO-Link – 83 – Version V1.1.5

### 669 6.6.6 UART frame eye-diagram with maximum load (Device)

### 670 Table 43 defines the test conditions for this test case.

### 671 Table 44 – UART frame eye-diagram with maximum load (Device)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0296

Name TCD_PHYL_INTF_UARTEYEMAXLOAD

Purpose (short) Eye-diagram of the UART frame

Equipment under test (EUT) Device

Test case version 1.2

Category / type Device signal test: test to pass

Specification (clause) [1], see 5.3.3.2, Figure 23, Table 9

Configuration / setup Reference-Master connected to EUT (see Figure A.15 and A.1.5) 672

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The waveform shall meet the requirements of the eye-diagram under maximum line load conditions. Test waveform for UART frame at the C/Q input on the receiver side with an applied maximum permissible line load. UART frames shall provide rising and falling edges at every bit position.

Precondition Memorized waveforms of TC_0294

Procedure –

Test parameter –

Post condition – 673

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Apply mask with keep-out areas according to [1], Figure 23 to recorded UART frame waveforms of TC_0294

Test passed UART frame waveform does not enter keep-out areas

Test not passed (examples) Any UART frame waveform enters keep-out areas

Report UART frame eye-diagram @ maximum load (VSM = 20 V): <yes/no> <ok | nok> UART frame eye-diagram @ maximum load (VSM = 30 V): <yes/no> <ok | nok>

674

675

### Version V1.1.5 – 84 – Test Specification © IO-Link

### 676 6.6.7 UART frame eye-diagram with minimum load (Master)

### 677 Table 45 defines the test conditions for this test case.

### 678 Table 45 – UART frame eye-diagram with minimum load (Master)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0033

Name TCM_PHYL_INTF_UARTEYEMINLOAD

Purpose (short) Eye-diagram of UART frame

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master signal test: test to pass

Specification (clause) [1], see 5.3.3.2, Figure 23, Table 9

Configuration / setup EUT and Reference-Device connected without line simulation (see A.1.5) 679

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The waveform shall meet the requirements of the eye-diagram under minimum line load conditions. Test waveform for UART frame at the C/Q input on the receiver side with a minimum load. UART frames shall provide rising and falling edges at every bit position.

Precondition Memorized waveforms of TC_0031

Procedure –

Test parameter –

Post condition – 680

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Apply mask with keep-out areas according to [1], Figure 23 to recorded UART frame waveforms of TC_0031

Test passed UART frame waveforms do not enter keep-out areas

Test not passed (examples) Any UART frame waveform enters keep-out areas

Report UART frame eye-diagram @ minimum load (PSMmin): <yes/no> <ok | nok> UART frame eye-diagram @ minimum load (PSMmax): <yes/no> <ok | nok>

681

682

### Test Specification © IO-Link – 85 – Version V1.1.5

### 683 6.6.8 UART frame eye-diagram with minimum load (Device)

### 684 Table 45 defines the test conditions for this test case.

### 685 Table 46 – UART frame eye-diagram with minimum load (Device)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0297

Name TCD_PHYL_INTF_UARTEYEMINLOAD

Purpose (short) Eye-diagram of UART frame

Equipment under test (EUT) Device

Test case version 1.2

Category / type Device signal test: test to pass

Specification (clause) [1], see 5.3.3.2, Figure 23, Table 9

Configuration / setup Reference-Master connected to EUT without line simulation (see A.1.5) 686

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The waveform shall meet the requirements of the eye-diagram under minimum line load conditions. Test waveform for UART frame at the C/Q input on the receiver side with a minimum load. UART frames shall provide rising and falling edges at every bit position.

Precondition Memorized waveforms of TC_0295

Procedure –

Test parameter –

Post condition – 687

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Apply mask with keep-out areas according to [1], Figure 23 to recorded UART frame waveforms of TC_0295

Test passed UART frame waveform is outside keep-out areas

Test not passed (examples) Any UART frame waveform is inside keep-out areas

Report UART frame eye-diagram @ minimum load (VSM = 20 V): <yes/no> <ok | nok> UART frame eye-diagram @ minimum load (VSM = 30 V): <yes/no> <ok | nok>

688

689

### Version V1.1.5 – 86 – Test Specification © IO-Link

### 690 6.6.9 UART frame transmission delay of Master (Ports)

### 691 Table 47 defines the test conditions for this test case.

### 692 Table 47 – UART frame transmission delay of Master (Ports)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0303

Name TCM_PHYL_INTF_UARTTRANSDELAY

Purpose (short) UART frame transmission delay is within permitted range

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master signal test: test to pass

Specification (clause) [1], see A.3.3, equation (A.3)

Configuration / setup EUT with Reference-Device and C/Q line monitor (Figure A.16) 693

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The delay time between two consecutive UART frames of a Master message is measured.

Precondition Reference-Device: Connected EUT: PORT_INACTIVE

Procedure a) Set Master to communication, e.g. via SMI_PortConfiguration(ABPS_NO_TYPE_CHECK) b) Record waveform at C/Q of at least 7 Master messages in STARTUP, PREOPERATE, and OPERATE c) Evaluation 1)

Test parameter –

Post condition – 694

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Determine time tFRAME between the rising edges of start bits of consecutive UART frames for all recorded Master messages. Calculate UART frame transmission delay t1 for all evaluated UART frames with: t1 = (tFRAME – 11 TBIT) Determine minimum and maximum values from all calculated delays t1

Test passed For all values t1: t1 ≤ 1 TBIT

Test not passed (examples) Any value t1: t1 > 1 TBIT

Report t1min: <minimum value of t1> t1max: <maximum value of t1> t1 within specified boundaries: <yes/no> <ok | nok>

695

696

### Test Specification © IO-Link – 87 – Version V1.1.5

### 697 6.6.10 UART frame transmission delay of Device

### 698 Table 48 defines the test conditions for this test case.

### 699 Table 48 – UART frame transmission delay of Device

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0304

Name TCD_PHYL_INTF_UARTTRANSDELAY

Purpose (short) UART frame transmission delay is within permitted range

Equipment under test (EUT) Device

Test case version 1.2

Category / type Device signal test: test to pass

Specification (clause) [1], see A.3.4, equation (A.4)

Configuration / setup Device connected to Reference-Master and C/Q line monitor (Figure A.16) 700

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The delay time between two consecutive UART frames of a Device reply message is measured.

Precondition Reference-Master: PORT_INACTIVE EUT: SIO mode

Procedure a) Set Master to SDCI mode, e.g. via SMI_PortConfiguration(ABPS_NO_TYPE_CHECK) b) Monitor waveform of at least 7 Device reply messages with more than 1 UART frame in STARTUP, PREOPERATE, and OPERATE at C/Q c) Evaluation 1)

Test parameter –

Post condition – 701

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Determine time tFRAME between the rising edges of start bits of consecutive UART frames for all recorded Device reply messages. Calculate UART frame transmission delay t2 for all evaluated UART frames with: t2 = (tFRAME – 11 TBIT)

Test passed For all values t2: t2 ≤ 3 TBIT

Test not passed (examples) Any value t2: t2 > 3 TBIT

Report t2min: <minimum value of t2> t2max: <maximum value of t2> t2 within specified boundaries: <yes/no> <ok | nok>

702

703

### Version V1.1.5 – 88 – Test Specification © IO-Link

### 704 6.6.11 Response time of Device

### 705 Table 49 defines the test conditions for this test case.

### 706 Table 49 – Response time of Device

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0305

Name TCD_PHYL_INTF_RESPONSETIME

Purpose (short) Device response time is within permitted range

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device signal test: test to pass

Specification (clause) [1], see A.3.5, equation (A.5)

Configuration / setup Device connected to Reference-Master and C/Q line monitor (Figure A.16) 707

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The delay time between Master messages to Device reply message (end of last UART frame to begin of first UART frame) is measured.

Precondition Reference-Master: PORT_INACTIVE EUT: SIO mode

Procedure a) Set Master to SDCI mode, e.g. via SMI_PortConfiguration(ABPS_NO_TYPE_CHECK) b) Monitor all M-sequences with Master Read messages in STARTUP c) Monitor all M-sequences with Master Write messages in STARTUP d) Monitor all M-sequences with Master Read messages in PREOPERATE e) Monitor all M-sequences with Master Write messages in PREOPERATE f) Monitor at least 100 M-sequences with Master Read messages in OPERATE g) Monitor at least 100 M-sequences with Master Write messages in OPERATE h) Monitor all M-sequences with 4 Device Events in OPERATE if applicable i) Evaluation 1)

Test parameter Exemplary: Device test Events according to IODD

Post condition – 708

TEST CASE RESULTS CHECK / REACTION

Evaluation For all values tA:

1. Determine time tDELAY between the rising edges of the start bit of the last UART

frame of the Master message to the start bit of the first UART frame of the Device reply message for all recorded M-sequences. Calculate response time of the Device tA for all evaluated M-sequences with: tA = (tDELAY – 11 TBIT)

Test passed All values tA: 1 TBIT ≤ tA ≤ 10 TBIT

Test not passed (examples) Any value tA: tA < 1 TBIT OR tA > 10 TBIT

Report tAmin: < minimum value of tA > tAmax: < maximum value of tA > tA within specified boundaries: <yes/no> <ok | nok>

709

710

711

### Test Specification © IO-Link – 89 – Version V1.1.5

### 712 6.6.12 Device response without transmission errors

### 713 Table 50 defines the test conditions for this test case.

### 714 Table 50 – Device response without transmission errors

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0319

Name TCD_PHYL_INTF_TRANSMISSIONERRORS

Purpose (short) Device response without transmission errors @ standard noise-free test conditions

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device signal test: test to pass

Specification (clause) [1], Annex I, Figure I.1

Configuration / setup Reference-Master connected to EUT and C/Q line monitor (Figure A.16) 715

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device response without transmission errors at standard noise-free test conditions:

- Parity

- Checksum

- Missing Device response

Precondition Reference-Master: PORT_INACTIVE EUT: SIO mode

Procedure a) Set Master to SDCI mode, e.g. via SMI_PortConfiguration(ABPS_NO_TYPE_CHECK) b) Monitor all M-sequences with Master read messages in STARTUP c) Monitor all M-sequences with Master write messages in STARTUP d) Monitor all M-sequences with Master read messages in PREOPERATE e) Monitor all M-sequences with Master write messages in PREOPERATE f) Monitor at least 100 M-sequences with Master read messages in OPERATE g) Monitor at least 100 M-sequences with Master write messages in OPERATE h) Monitor all M-sequences with four Master Events in OPERATE if applicable j) Evaluation 1)

Test parameter –

Post condition – 716

TEST CASE RESULTS CHECK / REACTION

Evaluation Count transmission errors in all recorded Device responses

Test passed No transmission error detected

Test not passed (examples) Transmission error detected

Report Transmission errors: <yes/no> <ok | nok>

717

718

### Version V1.1.5 – 90 – Test Specification © IO-Link

### 719 6.7 Test report template for PL tests

### 720 Table 51 shows the template for the test report of the Physical Layer tests.

### 721 Table 51 – Template for the test report of the Physical Layer tests

TEST CASE ID TEST results ok/ nok Statement/ Exception

SDCI_TC_0001 VSM (PSMmin): <value> VSM (PSMmax): <value> Steady current flow: <yes/no>

SDCI_TC_0002 QMI (PSMmin): <value> QMI (PSMmax): <value>

SDCI_TC_0003 ILLM (VIM = 5 V, PSMmin): <value> ILLM (VIM = 5,1 V, PSMmin): <value> ILLM (VIM = 15 V, PSMmin): <value> ILLM (VIM = measured value of VSM, PSMmin): <value> ILLM (VIM = 5 V, PSMmax): <value> ILLM (VIM = 5,1 V, PSMmax): <value> ILLM (VIM = 15 V, PSMmax): <value> ILLM (VIM = measured value of VSM, PSMmax): <value>

SDCI_TC_0004 VRQHM (PSMmin): <value> VRQHM (PSMmax): <value>

SDCI_TC_0005 VRQLM (PSMmin): <value> VRQLM (PSMmax): <value>

SDCI_TC_0006 VIM @ Transition 0→1 (PSMmin): <value> VIM @ Transition 0→1 (PSMmax): <value>

SDCI_TC_0007 VIM @ Transition 1→0 (PSMmin): <value> VIM @ Transition 1→0 (PSMmax): <value>

SDCI_TC_0008 VHYSM (PSMmin): <value> VHYSM (PSMmax): <value>

SDCI_TC_0299 Communication established: <yes/no>

SDCI_TC_0310 RIsoM0: <value> RIsoM1: <value>

SDCI_TC_0011 ISDSIOmax (VSD = 18 V): <value/ n/a> ISDSIOmax (VSD = 30 V): <value/ n/a> ISDIOLmax (VSD = 18 V): <value> ISDIOLmax (VSD = 30 V): <value>

Warning

SDCI_TC_0320 Type of information: <value> Warning

SDCI_TC_0012 QISD (VSD = 18 V): <value> STARTUP count (VSD = 18 V): <value> QISD (VSD = 30 V): <value> STARTUP count (VSD = 30 V): <value>

SDCI_TC_0013 VCQ (VSD = 18 V): <value> VCQ (VSD = 30 V): <value>

SDCI_TC_0014 VCQ (VSD = 18 V): <value> VCQ (VSD = 30 V): <value>

SDCI_TC_0015 ICQ (VSD = 18 V, VID = 13V): <value> ICQ (VSD = 18 V, VID = VSD): <value> ICQ (VSD = 30 V, VID = 13V): <value> ICQ (VSD = 30 V, VID = VSD): <value>

SDCI_TC_0016 VID @ Transition 0→1 (VSD = 18 V): <value> VID @ Transition 0→1 (VSD = 30 V): <value>

SDCI_TC_0017 VID @ Transition 1→0 (VSD = 18 V): <value> VID @ Transition 1→0 (VSD = 30 V): <value>

SDCI_TC_0018 VHYSD (VSD = 18 V): <value> VHYSD (VSD = 30 V): <value>

SDCI_TC_0300 Communication established: <yes/no>

SDCI_TC_0311 RIsoD0: <value> RIsoD1: <value>

### Test Specification © IO-Link – 91 – Version V1.1.5

TEST CASE ID TEST results ok/ nok Statement/ Exception

SDCI_TC_0021 VIM @ WURQ (PSMmin): <value> VIM @ WURQ (PSMmax): <value>

SDCI_TC_0022 tWUmin @ WURQ: <value> tWUmax @ WURQ: <value>

SDCI_TC_0023 VIM @ WURQ (PSMmin): <value> VIM @ WURQ (PSMmax): <value>

SDCI_TC_0024 tWUmin @ WURQ: <value> tWUmax @ WURQ: <value>

SDCI_TC_0025 WURQ (VSD = 18 V): <yes/no> WURQ (VSD = 30 V): <yes/no> Proof of correct pulse detection: <yes/no>

SDCI_TC_0026 WURQ (VSD = 18 V): <yes/no> WURQ (VSD = 30 V): <yes/no> Proof of correct pulse detection: <yes/no>

SDCI_TC_0027 tREN @ C/Q = high: <value>

SDCI_TC_0028 tREN @ C/Q = low: <value>

SDCI_TC_0029 Response to first Wake-up request @TRDL: <yes/no>

SDCI_TC_0301 tDSIOmin: <value> tDSIOmax: <value> tDSIO within specified boundaries: <yes/no>

SDCI_TC_0302 tFBD @ OPERATE: <value> tFBD @ PREOPERATE: <value>

SDCI_TC_0030 Bit eye-diagram @ maximum load (PSMmin): <yes/no> Bit eye-diagram @ maximum load (PSMmax): <yes/no>

SDCI_TC_0294 Bit eye-diagram @ maximum load (VSM = 20 V): <yes/no> Bit eye-diagram @ maximum load (VSM = 30 V): <yes/no>

SDCI_TC_0031 Bit eye-diagram @ minimum load (PSMmin): <yes/no> Bit eye-diagram @ minimum load (PSMmax): <yes/no>

SDCI_TC_0295 Bit eye-diagram @ minimum load (VSM = 20 V): <yes/no> Bit eye-diagram @ minimum load (VSM = 30 V): <yes/no>

SDCI_TC_0032 UART frame eye-diagram @ maximum load (PSMmin): <yes/no> UART frame eye-diagram @ maximum load (PSMmax): <yes/no>

SDCI_TC_0296 UART frame eye-diagr. @ maximum load (VSM = 20 V): <yes/no> UART frame eye-diagr. @ maximum load (VSM = 30 V): <yes/no>

SDCI_TC_0033 UART frame eye-diagram @ minimum load (PSMmin): <yes/no> UART frame eye-diagram @ minimum load (PSMmax): <yes/no>

SDCI_TC_0297 UART frame eye-diagr. @ minimum load (VSM = 20 V): <yes/no> UART frame eye-diagr. @ minimum load (VSM = 30 V): <yes/no>

SDCI_TC_0303 t1min: <minimum value of t1> t1max: <maximum value of t1> t1 within specified boundaries: <yes/no>

SDCI_TC_0304 t2min: <minimum value of t2> t2max: <maximum value of t2> t2 within specified boundaries: <yes/no>:

SDCI_TC_0305 tAmin: < minimum value of tA > tAmax: < maximum value of tA > tA within specified boundaries: <yes/no>:

SDCI_TC_0319 Transmission errors: <yes/no>:

722

723

### Version V1.1.5 – 92 – Test Specification © IO-Link

### 724 7 Device protocol test cases

### 725 7.1 General

### 726 The protocol tests can be performed almost automatically with the help of a Device-Tester as

### 727 defined in A.2.2. The test sequences are described in 5.4 together with a list of the relevant

### 728 test cases for Devices without ISDU support in Table 4, and a list of the relevant test cases for

### 729 Devices with ISDU support in Table 5. Supplementary requirements for Legacy-Devices beyond

### 730 the definitions in [5] are listed in B.

### 731 Procedure results not mentioned in the evaluation part are assumed to be successful for pass-

### 732 ing the tests.

### 733 7.2 STARTUP

### 734 7.2.1 STARTUP cycle time

### 735 Table 52 defines the test conditions for this test case.

### 736 Table 52 – STARTUP cycle time

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0034

Name TCD_DLPC_STUP_CYCTIME

Purpose (short) Test STARTUP cycle time

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device protocol test: test to pass

Specification (clause) [1], see 7.3.2.5, 9.3.3.2, and A.2.6

Configuration / setup Device-Tester-Unit 737

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Device shall perform in state STARTUP at any cycle time ≥ 100 TBIT

Precondition DTU: Wake-up and ComRequest are achieved until STARTUP is reached EUT: Device is in SDCI mode

Procedure a) Master takes first value of CycTime in field "Test parameter" b) Master reads communication parameter (Direct Parameter 0x02 to 0x06) c) Master repeats at b) with next value of CycTime

Test parameter CycTime {100 TBIT, 10 000 TBIT, 10 s}

Post condition – 738

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Device response after b)

Test passed The Device responds to all read requests with valid (constant) data

Test not passed (examples) No or incorrect response at any of the read requests

Report Cycle time variation in STARTUP: <ok | nok>

739 740

### Test Specification © IO-Link – 93 – Version V1.1.5

### 741 7.2.2 From STARTUP to OPERATE

### 742 Table 53 defines the test conditions for this test case.

### 743 Table 53 – From STARTUP to OPERATE

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0035

Name TCD_DLPC_STUP_STUPOPER1

Purpose (short) Test state transition STARTUP to OPERATE

Equipment under test (EUT) Device, except those with M-sequence TYPE_0 in OPERATE

Test case version 1.5

Category / type Device protocol test: test to pass and test to fail

Specification (clause) [1], see 7.3.2.5, 9.3.3.2

Configuration / setup Device-Tester-Unit 744

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) A Device supporting protocol revision V1.0 shall switch from STARTUP to OPERATE if a legacy master is connected. A Device not supporting protocol revision V1.0 shall not switch from STARTUP to OPERATE.

Precondition DTU: Wake-up and ComRequest are achieved until STARTUP is reached EUT: Device is in SDCI mode

Procedure a) Master reads communication parameters (Direct Parameter 0x02 to 0x06) b) Master takes first value of MasterCycleTime in field "Test parameter" c) Master sends MasterCycleTime d) Master sends Master command 0x99 "DeviceOperate" e) Master sends ISDU "idle" request using the legacy M-sequence TYPE for OPERATE,Table

### A.9, M-sequence types OPERATE mode using legacy protocol in [1].

f) Check Device response g) Master and Device switch to STARTUP h) Repeat from c) with next MasterCycleTime in field "Test parameter"

Test parameter MasterCycleTime {MinCycleTime of Device, 0xBF (= 132,8 ms)}

Post condition – 745

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) If Device supports protocol revision V1.0, check in step f) whether Device responds using M-Sequence TYPE for OPERATE

2. If Device does not support protocol revision V1.0, check in step f) whether Device

is not responding

Test passed The Device responds correctly to any request according to its protocol support

Test not passed (examples) The Device does not respond correctly to any request according to its protocol support or timeout

Report Transition from STARTUP directly to OPERATE: <ok | nok>

746 747

### Version V1.1.5 – 94 – Test Specification © IO-Link

### 748 7.2.3 Master start-up with overwrite of the RID (compatible)

### 749 Table 54 defines the test conditions for this test case.

### 750 Table 54 – Master start-up with overwrite of the RID (compatible)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0306

Name TCD_DLPC_CHCK_OVERRIDOK

Purpose (short) Check Device start-up behavior with overwrite of the Device RID (compatible)

Equipment under test (EUT) Device, except those not supporting protocol revision backward compatibility

Test case version 1.2

Category / type Device protocol test: test to pass

Specification (clause) [1], 10.6.3

Configuration / setup Device-Tester-Unit 751

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Device start-up behavior with overwrite of the RevisionID (compatible). This test supposes the active switching to protocol revision 1.0. The Master overwrites the RID and the Device accepts the requested protocol version.

Precondition DTU: SIO mode; Port is configured to RevisionID 0x10 EUT: RevisionID is set to factory settings (0x11)

Procedure a) Master performs WURQ, ComRequest, Startup up to MasterIdent b) Master detects incorrect "RevisionID" c) Master overwrites the RevisionID with the requested legacy RevisionID d) Master writes MasterCommand 0x96 e) Master reads communication parameters again f) Master switches the "modified" Device into OPERATE mode

Test parameter –

Post condition – 752

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After step e), check RevisionID

2. After step f), check Device response

Test passed RevisionID (RID) = 0x10, and Device response with M-sequence TYPE for OPERATE

Test not passed (examples) No response, or Device provides RID = 0x11, or Incorrect Device response

Report Active switching to legacy protocol revision accepted: <ok | nok>

753

754

755

### Test Specification © IO-Link – 95 – Version V1.1.5

### 756 7.2.4 Illegal STARTUP to OPERATE

### 757 Table 55 defines the test conditions for this test case.

### 758 Table 55 – Illegal STARTUP to OPERATE

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0036

Name TCD_DLPC_STUP_STUPOPER2

Purpose (short) Test illegal state transition STARTUP to OPERATE

Equipment under test (EUT) Device, except those with M-sequence TYPE_0 in OPERATE

Test case version 1.2

Category / type Device protocol test: test to pass

Specification (clause) [1], see 7.2.3.5, 9.3.3.2

Configuration / setup Device-Tester-Unit 759

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Device shall not switch from STARTUP to OPERATE unless it receives a Master command 0x99.

Precondition DTU: Wake-up and ComRequest are achieved until STARTUP is reached EUT: Device is in SDCI mode

Procedure a) Master reads communication parameters (Direct Parameter 0x02 to 0x06) b) Master sends MasterCycleTime c) Master sends ISDU "idle" request using the M-sequence TYPE for OPERATE

Test parameter –

Post condition – 760

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After step c), check Device response

Test passed No Device response

Test not passed (examples) Any Device response

Report Device does not leave STARTUP: <ok | nok>

761

762

### Version V1.1.5 – 96 – Test Specification © IO-Link

### 763 7.2.5 From OPERATE to STARTUP via Master command

### 764 Table 56 defines the test conditions for this test case.

### 765 Table 56 – From OPERATE to STARTUP via Master command

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0037

Name TCD_DLPC_OPER_OPERSTUP1

Purpose (short) Test correct state transition from OPERATE to STARTUP

Equipment under test (EUT) Device, except those with M-sequence TYPE_0 in OPERATE

Test case version 1.2

Category / type Device protocol test: test to pass

Specification (clause) [1], see 7.2.3.5, 9.3.3.2

Configuration / setup Device-Tester-Unit 766

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct state transition from OPERATE to STARTUP via Master command

Precondition DTU: SDCI communication, OPERATE EUT: Device is in SDCI mode

Procedure a) Master sends MasterCommand 0x97 "DeviceStartup" b) Master sends ISDU "idle" request using the M-sequence TYPE for OPERATE c) Master reads Direct Parameter page address 0x02 using M-Sequence TYPE_0

Test parameter –

Post condition – 767

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After step b), check Device response

2. After step c), check Device response

Test passed No response in 1), and Correct response in 2)

Test not passed (examples) Any response in 1), or Incorrect response in 2)

Report Transition from OPERATE state to STARTUP state: <ok | nok>

768

769

### Test Specification © IO-Link – 97 – Version V1.1.5

### 770 7.2.6 From OPERATE to STARTUP via M-sequence TYPE_0

### 771 Table 57 defines the test conditions for this test case.

### 772 Table 57 – From OPERATE to STARTUP via M-sequence TYPE_0

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0038

Name TCD_DLPC_OPER_OPERSTAR2

Purpose (short) Test state transition OPERATE to STARTUP

Equipment under test (EUT) Device, except those with M-sequence TYPE_0 in OPERATE

Test case version 1.3

Category / type Device protocol test: test to pass (positive testing)

Specification (clause) [1], see 7.2.3.5, 9.3.3.2; see NOTE in Table A.9

Configuration / setup Device-Tester-Unit 773

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test state transition OPERATE to STARTUP

Precondition DTU: SDCI communication, OPERATE EUT: Device is in SDCI mode

Procedure a) Master sends M-sequence TYPE_0 to read Direct Parameter page address 0x02 b) Master sends M-sequence TYPE_0 to read Direct Parameter page address 0x02 c) Master sends ISDU "idle" request, using the M-sequence TYPE for OPERATE

Test parameter –

Post condition – 774

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After step a), check Device response

2. After step b), check Device response

3. After step c), check Device response

Test passed No response in 1), and Correct response in 2), and No response in 3)

Test not passed (examples) Any response in 1), or Incorrect response in 2), or Any response in 3)

Report Incorrect M-sequence in OPERATE state: <ok | nok>

775

776

### Version V1.1.5 – 98 – Test Specification © IO-Link

### 777 7.3 PREOPERATE

### 778 7.3.1 From STARTUP to PREOPERATE Read

### 779 Table 58 defines the test conditions for this test case.

### 780 Table 58 – From STARTUP to PREOPERATE Read

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0039

Name TCD_DLPC_PROP_READDPP1

Purpose (short) Switch Device from STARTUP to PREOPERATE and read DPP1.

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device protocol test: test to pass, test to fail

Specification (clause) [1], see 7.3.2.5, Figure 34, and Annex A.2, Table A.7

Configuration / setup Device-Tester-Unit 781

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Switch Device from STARTUP to PREOPERATE via Master command 0x9A and read DPP1. Device activates On-request Data, ISDU and Event handler and returns DL_Mode.ind (PREOPERATE). Device reply message to Master read message to be checked.

Precondition - Initialize communication (WURQ)

- Communication initialization successful (both in STARTUP state)

- Save M-sequenceCapability, PDIn and PDOut for later comparison

Procedure a) DTU sends MasterCommand PREOPERATE (0x9A) b) DTU read message: Read DPP1

Test parameter M-sequenceCapability, PDIn, and PDOut

Post condition – 782

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether MasterCommand 0x9A results in a correct reaction of the Device

2. Determine with saved settings on PDIn, PDOut, and M-sequenceCapability the

expected M-sequenceTYPE

3. Check whether the Device reply message has been received with the expected

amount of On-request Data octets.

4. Check whether no process data has been transmitted.

Test passed Device reply message has been received with the expected amount of On-request Data octets

Test not passed (examples) In 1) MasterCommand 0x95 results in a state ≠ PREOPERATE, or In 3), 4) No or incorrect response from the Device

Report Read of DPP1 in PREOPERATE state: <ok | nok>

783

784

### Test Specification © IO-Link – 99 – Version V1.1.5

### 785 7.3.2 From STARTUP to PREOPERATE Write

### 786 Table 59 defines the test conditions for this test case.

### 787 Table 59 – From STARTUP to PREOPERATE Write

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0040

Name TCD_DLPC_PROP_WRITEDPP1

Purpose (short) Switch Device from STARTUP to PREOPERATE and write DPP1.

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device protocol test: test to pass, test to fail

Specification (clause) [1], see 7.3.2.5, Figure 34, and Annex A.2, Table A.7

Configuration / setup Device-Tester-Unit 788

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Switch Device from STARTUP to PREOPERATE state via MasterCommand 0x9A and read DPP1. Master writes DPP1. Device reply message to Master read message to be checked. NOTE The number of octets to write depends on the used M-sequenceTYPE. The DPP1 will not be completely written, but a write request with a valid M-sequenceTYPE length will be accepted.

Precondition - Initialize communication (WURQ)

- Communication initialization between Master and Device has been successful

(both in STARTUP)

- Save M-sequenceCapability, PDIn, and PDOut for later comparison

Procedure a) DTU sends MasterCommand PREOPERATE (0x9A). b) Device acknowledges command c) DTU and Device changed to PREOPERATE d) Read DPP1 (one M-sequence) e) Save the Device's response On-request Data f) DTU builds a write message with the saved On-request Data ("mirror") g) DTU writes DPP1 (one message) in correct length h) Receive Device response

Test parameter M-sequenceCapability, PDIn and PDOut

Post condition – 789

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether MasterCommand 0x9A results in correct response of the Device

2. Determine with saved settings on PDIn, PDOut, and M-sequenceCapability the

expected M-sequenceTYPE

3. Check whether the Device reply message has been received and contains no On-

request Data octets

Test passed DPP1 write command has been accepted

Test not passed (examples) In 1) MasterCommand 0x9A results in an incorrect response of the Device In 2), 3) No or incorrect response from the Device

Report Write of DPP1 in PREOPERATE state: <ok | nok>

790

791

### Version V1.1.5 – 100 – Test Specification © IO-Link

### 792 7.3.3 From STARTUP to PREOPERATE short message

### 793 Table 60 defines the test conditions for this test case.

### 794 Table 60 – From STARTUP to PREOPERATE short message

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0041

Name TCD_DLPC_PROP_SHORTMESSAGE

Purpose (short) Test behavior to truncated M-sequence request

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device protocol test: test to fail

Specification (clause) [1], see 7.3.2.5, Figure 34, and Annex A.2, Table A.7

Configuration / setup Device-Tester-Unit 795

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Switch Device into PREOPERATE. To emulate message disturbances, caused for example by electromagnetic interference, DTU sends one octet less than required. The Device shall not respond to this truncated M-sequence request and respond to the following request without error.

Precondition DTU and Device in PREOPERATE

Procedure a) DTU writes to parameter "VendorID" in DPP1 (address 0x07/0x08) with one octet less than the normal request length b) DTU writes to parameter "VendorID" in DPP1 (address 0x07/0x08) after the shortest possible time (MinCycleTime, see Table A.9 and B.1.4)

Test parameter –

Post condition – 796

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response in a)

2. Check response in b)

Test passed In 1) No response on the first request, and In 2) Response on the second request

Test not passed (examples) In 1) Response on the first request, or In 2) No response to the second request

Report First response: <ok | nok> Second response: <ok | nok>

797

798

### Test Specification © IO-Link – 101 – Version V1.1.5

### 799 7.3.4 From PREOPERATE to STARTUP via simulated reset

### 800 Table 61 defines the test conditions for this test case.

### 801 Table 61 – From PREOPERATE to STARTUP via simulated reset

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0043

Name TCD_DLPC_PROP_SIMRESET

Purpose (short) Switch Device back to STARTUP from PREOPERATE via simulation of a reset

Equipment under test (EUT) Device in PREOPERATE

Test case version 1.2

Category / type Device protocol test: test to pass, test to fail

Specification (clause) [1][1], see 7.3.2.5, Figure 34 (T12), and Annex A, Table A.7; see NOTE in Table A.9 on TYPE_0

Configuration / setup Device-Tester-Unit 802

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master and Device are in PREOPERATE. Master sends a TYPE_0 message (simulation of a Master reset – FHInfo_ILLEGAL_FRAMETYPE). The Device shall switch to STARTUP (deactivate On-request Data, ISDU and Event handler) and shall send a TYPE_0 response.

Precondition - Establish communication (WURQ)

- Communication between DTU and Device successful (both in STARTUP)

- Save M-sequenceCapability, PDIn, and PDOut for later comparison

- DTU sends MasterCommand PREOPERATE (0x9A)

- DTU and Device in PREOPERATE

Procedure a) DTU checks usage of TYPE_0 in PREOPERATE. If yes, stop test and raise exception. Otherwise: b) DTU sends TYPE_0 Read request to get MinCycleTime c) DTU sends TYPE_0 read request to get MinCycleTime again

Test parameter M-sequenceCapability, PDIn, and PDOut

Post condition – 803

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether the Master TYPE_0 request in b) results in no response message from the Device

2. Check whether the Master TYPE_0 request in c) results in a response message

of TYPE_0 with "MinCycleTime" from the Device

Test passed Received "MinCycleTime" with TYPE_0 message after second Read attempt

Test not passed (examples) Any response of the Device in procedure step b), or Device responds in procedure step c):

- Device used incorrect M-sequence TYPE

- No or incorrect response from Device

Report Received "MinCycleTime" in TYPE_0 Device message: <ok | nok | exception>

804

805

### Version V1.1.5 – 102 – Test Specification © IO-Link

### 806 7.3.5 From PREOPERATE to STARTUP with M-sequence fault

### 807 Table 62 defines the test conditions for this test case.

### 808 Table 62 – From PREOPERATE to STARTUP with M-sequence fault

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0044

Name TCD_DLPC_PROP_FRAMEFAULT

Purpose (short) Force Device into STARTUP by sending the OPERATE M-sequence type

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device protocol test: test to pass, test to fail

Specification (clause) [1], see 7.3.2.5, Figure 34 (T12), and Annex A, Table A.7; see NOTE in Table A.9 on TYPE_0

Configuration / setup Device-Tester-Unit 809

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device shall not support another different M-sequence type while in PREOPERATE. The Device shall switch to STARTUP when detecting an illegal M-sequence type. Equal M-sequence types for PREOPERATE and OPERATE are legal, and M-sequence types TYPE_1_2 and TYPE_1_V are not treated as different.

Precondition Device in PREOPERATE

Procedure a) Determine expected M-sequence type and OD length from PDIn, PDOut, and MsequenceCapability b) Read parameter "MinCycleTime" with the OPERATE M-sequence types c) Read again parameter "MinCycleTime" with the PREOPERATE M-sequence types d) Read again parameter "MinCycleTime" with the STARTUP M-sequence type

Test parameter –

Post condition – 810

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read response in b) with respect to used M-sequence types

2. Check Read response in c) with respect to used M-sequence types

3. Check Read response in d) with respect to used M-sequence types

Test passed Case a: PREOPERATE = TYPE_0 and ≠ OPERATE M-sequence types:

- No Device response at Read in b)

- Correct data at Read in c) and d)

- Implementation exception due to use of TYPE_0

Case b: PREOPERATE ≠ TYPE_0 and ≠ OPERATE M-sequence types:

- No Device response at Read in b) and c)

- Correct data at Read in d)

Case c: PREOPERATE = TYPE_0 and = OPERATE M-sequence types:

- Correct data at Read in b), c), and d)

- Implementation exception due to use of TYPE_0

Case d: PREOPERATE ≠ TYPE_0 and = OPERATE M-sequence types:

- No Device response at Read in d)

Test not passed (examples) Any evaluation failed

Report Tests in case a: <ok | nok | exception> Tests in case b: <ok | nok > Tests in case c: <ok | nok | exception> Tests in case d: <ok | nok >

811 812 813 814 815 816

### Test Specification © IO-Link – 103 – Version V1.1.5

### 817 7.3.6 Minimum Recovery Time in PREOPERATE Mode

### 818 Table 63 defines the test conditions for this test case.

### 819 Table 63 – From Minimum Recovery Time in PREOPERATE Mode

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0377

Name TCD_DLPC_PROP_RECOVERY_TIME

Purpose (short) Check if Device works with minimum recovery time in PREOPERATE mode

Equipment under test (EUT) Device with ISDU support

Test case version 1.0

Category / type Device protocol test: test to pass

Specification (clause) [1], Table A.8

Configuration / setup Device-Tester-Unit 820

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The DTU shall use the minimum recovery time in PREOPERATE. The test case checks if the Device answers properly on each Master M-sequence.

Precondition DTU: in PREOPERATE mode, DTU shall use minimum recovery time as specified in [1], Table A.8.

Procedure a) DTU reads communication parameter (Direct Parameter 0x02 to 0x06) b) DTU writes to parameter A the value “test” c) DTU reads parameter A

Test parameter parameter A: application specific tag (index 24)

Post condition – 821

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Device response in a) to c): Device must answer to any M-Sequence. No repetitions are allowed

2. Check after c) if read value equals the value written in b) if the device resonse is

positive in step b)

Test passed All evaluation steps ok.

Test not passed (examples) Any evaluation step failed

Report All evaluations <ok | nok>

822

### Version V1.1.5 – 104 – Test Specification © IO-Link

### 823 7.4 OPERATE

### 824 7.4.1 From PREOPERATE to OPERATE Read

### 825 Table 64 defines the test conditions for this test case.

### 826 Table 64 – From PREOPERATE to OPERATE Read

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0045

Name TCD_DLPC_OPER_READ

Purpose (short) Turn Master and Device into OPERATE via MasterCommand 0x99 and 0x98

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device protocol test: test to pass

Specification (clause) [1], see 7.3.2.5, Figure 34, and Annex A.2, Table A.8

Configuration / setup Device-Tester-Unit 827

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends MasterCommand 0x99 and 0x98 and both (Device and Master) switch into OPERATE.

Precondition - Establish a communication (WURQ)

- Communication between Master and Device successful (both in STARTUP)

- Save M-sequenceCapability, PDIn, and PDOut for later comparison

- DTU sends MasterCommand PREOPERATE (0x9A)

- DTU and Device in PREOPERATE

- Read DPP1 and save it in a variable for further use

Procedure a) DTU sends MasterCommand 0x99 (OPERATE) followed by 0x98 (PD output valid) b) Read DPP1 for comparison with the appropriate M-sequence types for OP- ERATE c) DTU sends MasterCommand 0x99 (OPERATE) d) Read DPP1 for comparison with the appropriate M-sequence types for OP- ERATE

Test parameter M-sequenceCapability, PDIn, and PDOut

Post condition – 828

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check reaction of Device upon MasterCommand 0x99 and 0x98

2. Determine M-sequence type via saved settings of M-sequenceCapability, PDIn,

and PDOut

3. Check whether DPP1 values were received completely (see "Test parameters")

Test passed Device in OPERATE and DPP1 received correctly

Test not passed (examples) - State ≠ OPERATE

- No or incorrect response from Device

- Device did not use the expected M-sequence type

Report DPP1 received in OPERATE: <ok | nok >

829

830

### Test Specification © IO-Link – 105 – Version V1.1.5

### 831 7.4.2 From PREOPERATE to OPERATE Write

### 832 Table 65 defines the test conditions for this test case.

### 833 Table 65 – From PREOPERATE to OPERATE Write

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0046

Name TCD_DLPC_OPER_WRITE

Purpose (short) Turn Device from PREOPERATE to OPERATE write

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device protocol test: test to pass

Specification (clause) [1], see 7.3.2.5, Figure 34, and Annex A.2, Table A.8

Configuration / setup Device-Tester-Unit 834

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends MasterCommand 0x99 and 0x98 and both (Device and Master) switch to OPERATE. Master writes DPP1. Device reply message to Master write message to be checked. NOTE The number of octets to write depends on the used M-sequenceTYPE. The DPP1 will not be completely written, but a write request with a valid M-sequence type length will be accepted.

Precondition - Save M-sequenceCapability, PDIn, and PDOut for later comparison

- Master and Device in PREOPERATE state

Procedure a) DTU sends MasterCommand 0x99 (OPERATE) followed by 0x98 (PD output valid) b) Master changes to OPERATE c) Read DPP1 (one M-sequence) d) Save On-request Data of the Device's response ("mirror") e) Master prepares a write message with saved On-request Data f) Master writes DPP1 (one M-sequence) with correct length

Test parameter M-sequenceCapability, PDIn, and PDOut

Post condition – 835

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check reaction of Device upon MasterCommand 0x99 and 0x98

2. Determine M-sequence type via saved settings of M-sequenceCapability, PDIn,

and PDOut

3. Check Device response (write DPP1)

Test passed Device in OPERATE, and DPP1 accepted

Test not passed (examples) - State ≠ OPERATE

- No or incorrect response from Device

- Device did not use the expected M-sequence type

Report DPP1 write accepted in OPERATE: <ok | nok >

836

837

### Version V1.1.5 – 106 – Test Specification © IO-Link

### 838 7.4.3 From PREOPERATE to OPERATE negative Write

### 839 Table 66 defines the test conditions for this test case.

### 840 Table 66 – From PREOPERATE to OPERATE negative Write

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0047

Name TCD_DLPC_OPER_NEGWRITE

Purpose (short) Switch Device from PREOPERATE to OPERATE and check negative write response

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device protocol test: test to pass, test to fail

Specification (clause) [1], see 7.3.2.5, Figure 34, and Annex A.2, Table A.8

Configuration / setup Device-Tester-Unit 841

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Switch Device from PREOPERATE to OPERATE state via Master command 0x99 and 0x98. The Master writes DPP1 with one M-sequence and "parameter length underrun", i.e. one octet less than specified. Check whether the Device does not respond to this incomplete M-sequence. It is also a test purpose to send a correct M-sequence after the minimum cycle time of OPERATE and check the response.

Precondition Master and Device in PREOPERATE state

Procedure a) DTU sends MasterCommand 0x99 (OPERATE) followed by 0x98 (PD output valid) b) Master changes to OPERATE. c) Read DPP1 (one M-sequence) d) Save the On-request Data of the Device response e) Master prepares a write message with the saved On-request Data but one octet less than specified for the Master write message f) Master writes DPP1 (one M-sequence) with reduced length g) Master prepares a write message with the saved On-request Data in correct length h) Master writes DPP1 (one M-sequence) in correct length after the minimum cycle time of OPERATE

Test parameter –

Post condition – 842

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether MasterCommand 0x99 results in a positive response of the Device

2. Check whether Device response (write DPP1 with one octet less than expected)

results in no response

3. Check whether Device response (write DPP1 with correct length) results in an

expected Device reply message

Test passed All checks ok after using the incomplete M-sequence type

Test not passed (examples) - OPERATE command results in a negative response, or

- Device responds to message with incomplete M-sequence, or

- Device did not use the expected M-sequence type, or

- No Device response to the complete M-sequence

Report DPP1 write only accepted with specified length in OPERATE: <ok | nok >

843

844

### Test Specification © IO-Link – 107 – Version V1.1.5

### 845 7.4.4 From OPERATE to STARTUP via simulated reset

### 846 Table 67 defines the test conditions for this test case.

### 847 Table 67 – From OPERATE to STARTUP via simulated reset

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0049

Name TCD_DLPC_OPER_SIMRESET

Purpose (short) Switch Device from OPERATE back to STARTUP via a simulated reset

Equipment under test (EUT) Device without TYPE_0 in OPERATE

Test case version 1.2

Category / type Device protocol test: test to pass

Specification (clause) [1], see 7.3.2.5, Figure 34 (T11), and Annex A, Table A.8; see NOTE in Table A.9

Configuration / setup Device-Tester-Unit 848

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master and Device are in OPERATE. Master sends a TYPE_0 message (simulation of a Master reset). Device shall switch to STARTUP (deactivate On-request Data, ISDU and Event handler) and send a TYPE_0 response.

Precondition - Master and Device in PREOPERATE

- Master sends MasterCommand OPERATE (0x99)

- Master and Device in OPERATE

Procedure a) Master sends TYPE_0 Read request for the MinCycleTime parameter b) Master sends TYPE_0 Read request for the MinCycleTime parameter again

Test parameter –

Post condition – 849

TEST CASE RESULTS CHECK / REACTION

Evaluation Check whether the Master TYPE_0 request results in a Device TYPE_0 response message with the MinCycleTime parameter value

Test passed Received the MinCycleTime parameter value at second TYPE_0 message

Test not passed (examples) Any response of the Device in procedure step a) No or incorrect response from Device in procedure step b)

Report Received MinCycleTime value at second TYPE_0 message: <ok | nok >

850

851

### Version V1.1.5 – 108 – Test Specification © IO-Link

### 852 7.4.5 Actuator behavior at PDOut invalid

### 853 Table 68 defines the test conditions for this test case.

### 854 Table 68 – Actuator behavior at PDOut invalid

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0312

Name TCD_DLPC_OPER_OUTINVALID

Purpose (short) Failsafe reaction on PDOut invalid

Equipment under test (EUT) Device supporting PDOut

Test case version 1.0

Category / type Device protocol test: test to pass

Specification (clause) [1], clause 10.2

Configuration / setup Device-Tester-Unit 855

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device shall detect the change of PDOut validity state and react as described in the user manual

Precondition Device is in OPERATE and PDOut are marked as valid

Procedure a) Write MasterCommand 0x99 (OPERATE) to Device;Process output data invalid b) Observe Device reaction (failsafe state)

Test parameter –

Post condition – 856

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether Device acknowledged MasterCommand in a)

2. Check Device reaction against description in user manual in b)

Test passed Device changes state as described in user manual

Test not passed (examples) Device reaction deviates from expected reaction

Report Actuator state at PDOut invalid: <ok | nok >

857

858

### Test Specification © IO-Link – 109 – Version V1.1.5

### 859 7.4.6 Actuator behavior at communication loss

### 860 Table 69 defines the test conditions for this test case.

### 861 Table 69 – Actuator behavior at communication loss

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0313

Name TCD_DLPC_OPER_CONNECTIONLOSS

Purpose (short) Failsafe reaction on communication loss

Equipment under test (EUT) Device supporting PDOut

Test case version 1.0

Category / type Device protocol test: test to pass

Specification (clause) [1], clause 7.3.3.5, 10.2 and 10.8.3

Configuration / setup Device-Tester-Unit 862

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Device shall detect the loss of communication and react as described in the user manual

Precondition Device is in communication mode and the PDout are marked as valid

Procedure a) Communication stopped without MasterCommand "fallback", no further Wake-up b) Observe Device reaction (failsafe state)

Test parameter –

Post condition – 863

TEST CASE RESULTS CHECK / REACTION

Evaluation Check Device reaction after communication stop against description in user manual

Test passed Device changes state as described in user manual

Test not passed (examples) Device reaction deviates from expected reaction

Report Actuator state at COMLOSS: <ok | nok >

864

865

### Version V1.1.5 – 110 – Test Specification © IO-Link

### 866 7.4.7 PD Status for output Process Data only

### 867 Table 70 defines the test conditions for this test case.

### 868 Table 70 – PD Status for output Process Data only

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0376

Name TCD_DLPC_OPER_PDVALIDITYINDICATION

Purpose (short) Check PD Status flag for Device output Process Data only

Equipment under test (EUT) Device with output Process Data only

Test case version 1.0

Category / type Device protocol test; test to pass (positive testing)

Specification (clause) [7], A.1.5

Configuration / setup Device-Tester-Unit 869

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) A Device with no input Process Data but with output Process Data shall always indicate “Process Data valid” in the CKS octet

Precondition Device is in OPERATE and PDOut are marked as invalid

Procedure a) Master reads Checksum / status (CKS) octet from Device b) Write MasterCommand 0x98 (ProcessDataOutputOperate) to Device c) Master reads Checksum / status (CKS) octet from Device

Test parameter -

Post condition - 870

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step a) that PD Status flag indicates “Process Data valid”

2. Check after step c) that PD Status flag indicates “Process Data valid”

Test passed All evaluation steps ok.

Test not passed (example) Any evaluation step failed

Report Device indicates “Process Data valid” <ok | nok>

871

872

873

Test Specification © IO-Link – 111 – Version V1.1.5

874 7.5 ISDU (Indexed Service Data Unit)

875 7.5.1 Prearrangement measures and configuration

876 The possibility of write operations is a precondition for the ISDU test cases. The only possible 877 writeable Index usable by all Device vendors is the "Application Specific Tag" parameter (Index 878 = 0x0018). However, this parameter is optional.

879 Therefore, the vendor shall provide the necessary Index information for ISDU write/read oper- 880 ations (Config 1, 2, 3, and 7) within the IODD of the Device:

881 • Config1 (8-bit Index without ExtLength): An 8-bit readable and writeable SDCI Index of the 882 Device that shall not provide data of type StringT and data length shall be less than 12 883 octets (ISDU read operation shall not use the "ExtLength").

884 • Config2 (16-bit Index ISDU access): If the Device supports 16-bit Indices, Config2 shall be 885 a readable and writeable 16-bit Index. If the Device does not support any 16-bit Index, 886 Config2 can be any 16-bit Index. The Device shall respond with correct ErrorCodes in case 887 this Index is addressed. Via this Index, the test system will check the 16-bit capabilities 888 (coping with the 16-bit ISDU addressing scheme) of the Device.

889 • Config3 (8-bit Index with ExtLength): An 8-bit readable SDCI Index of the Device providing 890 more than 3 octets data for ISDU read operations with an "ExtLength" octet in an ISDU read 891 response.

892 • Config7 (IndexToGenerateEvent): This Index is a parameter for Devices supporting 893 Events. It can be used to stimulate up to two specific Test Events within the Device. Four 894 values shall be supported by the Device: 895 - EventA_Appear, 896 - EventA_Disappear, 897 - EventB_Appear, and 898 - EventB_Disappear.

899 900 A Write of this parameter causes the Device to stimulate the corresponding Event with the 901 corresponding Event mode. The data type of the parameter is UIntegerT, bitLength = 8. 902 There are Devices not supporting appearing and disappearing Events but supporting Noti- 903 fications. For the test of these kind of Events, the values for Event "appeared" and Event 904 "disappeared" shall be the same. The test system shall detect that the values are the 905 same and perform a Notification test.

906

907 In addition to the information in the IODD the Test System will automatically generate a list of 908 block parameters based on the Paramter set 1, see 7.7.1.1.

909

### Version V1.1.5 – 112 – Test Specification © IO-Link

### 910 7.5.2 Availability of ISDU via M-sequence Capability

### 911 Table 71 defines the test conditions for this test case.

### 912 Table 71 – Availability of ISDU via M-sequence Capability

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0052

Name TCD_DLPC_ISDU_AVAILFSEQCAP

Purpose (short) Availability of ISDU via M-sequenceCapability in DPP1

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device ISDU test: test to pass

Specification (clause) [1], see Annex B.1.4, Table B.3

Configuration / setup Device-Tester-Unit 913

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master reads the Device's M-sequenceCapability via DPP1 and checks whether the ISDU data channel is supported.

Precondition - Establish communication (WURQ)

- Establish communication into PREOPERATE or OPERATE respectively

Procedure a) Read M-sequenceCapability (DPP1, address 0x03) b) Check Bit "0" of the parameter M-sequenceCapability

Test parameter –

Post condition – 914

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Received M-sequenceCapability

2. Bit "0" = 1;ISDU communication channel is supported

Test passed Bit "0" = 1

Test not passed (examples) - No response from the Device

- Bit "0" = 0;ISDU communication channel is not supported

Report Availability of the ISDU service: <ok | nok >

915

916

### Test Specification © IO-Link – 113 – Version V1.1.5

### 917 7.5.3 "Idle/Busy" check

### 918 Table 72 defines the test conditions for this test case.

### 919 Table 72 – "Idle/Busy" check

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0053

Name TCD_DLIC_ISDU_IDLEBUSYCHECK

Purpose (short) Device response upon invalid FlowCtrl requests of the Master during "No Service" while in state “Idle_1”

Equipment under test (EUT) Device

Test case version 1.3

Category / type Device ISDU test: test to pass

Specification (clause) [1], see 7.3.6.2, Table 52, and Annex A.5, Table A.12, Table A.14

Configuration / setup Device-Tester-Unit 920

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device responds with "No Service" after reception of request of the Master with invalid FlowCtrl contents.

Precondition DTU: Communication without Parameter or Event access EUT: -

Procedure a) DTU sends Read requests with channel = ISDU and FlowCtrl from 0x11 to 0x1F

Test parameter –

Post condition – 921

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Device response in a)

Test passed Device response contains "No Service"

Test not passed (examples) No response from the Device, or Device response contains ≠ "No Service", or Communication error

Report ISDU FlowCtrl ignores invalid contents: <ok | nok >

922

923

### Version V1.1.5 – 114 – Test Specification © IO-Link

### 924 7.5.4 Read 8-bit Index

### 925 Table 73 defines the test conditions for this test case.

### 926 Table 73 – Read 8-bit Index

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0054

Name TCD_DLIC_ISDU_READINDEX8

Purpose (short) Device response to an ISDU Read request with 8-bit Index

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device ISDU test: test to pass

Specification (clause) [1], see 7.3.6.4, Figure 52, and Annex A.2, Table A.10

Configuration / setup Device-Tester-Unit 927

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends ISDU read request for VendorName (0x10, mandatory). Device responds with expected VendorName.

Precondition Master in PREOPERATE or OPERATE respectively

Procedure a) Master: Sends ISDU Read request to Index 0x10 b) Receive Read response "busy" (0x01) until Device is ready c) Check and save Read response ("temp") d) Save I-Service, Length, Data and Checksum in variables and save ISDU Read response M-sequence COUNT

Test parameter –

Post condition – 928

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether the I-Service code of the positive Read response = 0b1101

2. Compare Length variable with the actual received M-sequence COUNT

3. Check "temp"

4. Calculate checksum and compare with saved checksum

Test passed "temp" received is complete as expected as vendorName from IODD, and I-Service, Length and Checksum are correct

Test not passed (examples) No, negative or wrong response from the Device

Report "VendorName" read correctly from Device: <ok | nok >

929

930

### Test Specification © IO-Link – 115 – Version V1.1.5

### 931 7.5.5 Read 8-bit Index with ExtLength

### 932 Table 74 defines the test conditions for this test case.

### 933 Table 74 – Read 8-bit Index with ExtLength

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0055

Name TCD_DLIC_ISDU_READ8EXTLENGTH

Purpose (short) Read request with 8-bit Index and Read response with ExtLength

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device ISDU test: test to pass

Specification (clause) [1], see 7.3.6.4, Annex A.5.3

Configuration / setup Device-Tester-Unit 934

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends ISDU Read request using "Config3". Device responds according "Config3" (ISDU response with ExtLength octet possible).

Precondition - Device in PREOPERATE or OPERATE respectively

- DTU in PREOPERATE or OPERATE respectively

- Variable size to be set to > 13 octets

Procedure a) DTU: Sends ISDU Read request to Index defined in "Config3" b) Receive Read response "busy" (0x01) until Device is ready c) Save I-Service, Length, Data and Checksum in variables and save ISDU Read response M-sequence COUNT

Test parameter "Config3" according to 7.5.1

Post condition – 935

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether the I-Service code of the positive Read response = 0b1101

2. Length = 0b0001

3. Compare ExtLength variable with M-sequence COUNT

4. Calculate checksum and compare with saved Checksum

Test passed Response correct according to "Config3", and I-Service, Length, ExtLength and Checksum are correct

Test not passed (examples) No, negative or wrong response from the Device

Report "Config3" read correctly from Device: <ok | nok >

936

937

### Version V1.1.5 – 116 – Test Specification © IO-Link

### 938 7.5.6 Write 8-bit Index

### 939 Table 75 defines the test conditions for this test case.

### 940 Table 75 – Write 8-bit Index

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0056

Name TCD_DLIC_ISDU_WRITE8

Purpose (short) Write request with 8-bit Index is possible

Equipment under test (EUT) Device

Test case version 1.2

Category / type Device ISDU test: test to pass

Specification (clause) [1], see 7.3.6.4

Configuration / setup Device-Tester-Unit 941

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends ISDU write request according "Config1". Check whether the Write request has been successful.

Precondition - Device in PREOPERATE or OPERATE respectively

- DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends ISDU Write request using "Config1" b) Receive Write response "busy" (0x01) until Device is ready c) Receive Write response d) DTU sends ISDU Read request on Index of "Config1" e) Save received data as "temp"

Test parameter "Config1" according to 7.5.1. Manufacturer/vendor is responsible for choosing a variable for the test not changing value after Write.

Post condition – 942

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ISDU response in c)

2. Check received data in e)

Test passed Positive Write response and "temp" = Config1 from IODD or ErrorType ≠ "Index not available"

Test not passed (examples) Negative Write response ≠ "Index not available", or "temp" ≠ Config1 from IODD

Report 8-bit Write request: <ok | nok >

943

944

### Test Specification © IO-Link – 117 – Version V1.1.5

### 945 7.5.7 Read 8-bit Index reserved

### 946 Table 76 defines the test conditions for this test case.

### 947 Table 76 – Read 8-bit Index reserved

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0057

Name TCD_DLIC_ISDU_READ8RESERVED

Purpose (short) Device response “Index not available” upon Read of 8-bit reserved Index

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device ISDU test: test to fail

Specification (clause) [1], see 7.3.6, and Annex C, Table C.1

Configuration / setup Device-Tester-Unit 948

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends ISDU Read request for Index = 0xFF (reserved) and receives "Index not available".

Precondition - Device in PREOPERATE or OPERATE respectively

- DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends ISDU Read request on Index 0xFF, Subindex "0" b) Receive Read response "busy" (0x01) until Device is ready c) Receive Read response when ready

Test parameter –

Post condition – 949

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read response in c)

Test passed Negative Read response (I-Service: 0b1100, Length: 0b0100) with ErrorCode = 0x80, and AdditionalCode = 0x11 “Index not available”

Test not passed (examples) Positive response from Device or incorrect ErrorCode or AdditionalCode

Report Negative read response upon reserved 8-bit Index: <ok | nok >

950

951

### Version V1.1.5 – 118 – Test Specification © IO-Link

### 952 7.5.8 Read 8-bit Index with unavailable Subindex

### 953 Table 77 defines the test conditions for this test case.

### 954 Table 77 – Read 8-bit Index with unavailable Subindex

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0058

Name TCD_DLIC_ISDU_READ8NOSUBINDEX

Purpose (short) 8-bit Read response when Subindex not available

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device ISDU test: test to fail

Specification (clause) [1], see 7.3.6, and Annex C, Table C.1

Configuration / setup Device-Tester-Unit 955

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends ISDU Read request for VendorName (Index = 0x10, mandatory, StringT) and Subindex = 0x02. This results in a negative Read response, “Subindex not available”.

Precondition - Device in PREOPERATE or OPERATE respectively

- DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends ISDU Read request on Index 0x10, Subindex 2 b) Receive Read response "busy" (0x01) until Device is ready c) Receive Read response when ready

Test parameter –

Post condition – 956

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read response in c)

Test passed Negative Read response (I-Service: 0b1100, Length: 0b0100) with ErrorCode = 0x80, and AdditionalCode = 0x12 “Subindex not available”

Test not passed (examples) Positive response from Device or incorrect ErrorCode or AdditionalCode

Report Negative read response upon unavailable Subindex: <ok | nok >

957

958

### Test Specification © IO-Link – 119 – Version V1.1.5

### 959 7.5.9 Read 16-bit Index

### 960 Table 78 defines the test conditions for this test case.

### 961 Table 78 – Read 16-bit Index

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0059

Name TCD_DLIC_ISDU_READ16

Purpose (short) Read response with 16-bit Index using "Config2"

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device ISDU test: test to pass

Specification (clause) [1], see 7.3.6.4, Figure 52, and Annex A.2, Table A.10

Configuration / setup Device-Tester-Unit 962

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends ISDU read request using "Config2". Device responds according "Config2". If the Device does not support 16-bit Indices, access shall be rejected with the correct ErrorType.

Precondition - DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends ISDU Read request to Index defined in "Config2" b) Receive Read response "busy" (0x01) until Device is ready c) Save I-Service, Length, Data and Checksum in variables and save ISDU Read response

Test parameter "Config2" in 7.5.1

Post condition – 963

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

Test passed Positive read response or ErrorType "Index not available", and I-Service, Length and Checksum are correct

Test not passed (examples) Any other negative response or no response

Report "Config2" read correctly from Device: <ok | nok >

964

965

### Version V1.1.5 – 120 – Test Specification © IO-Link

### 966 7.5.10 Write 16-bit Index

### 967 Table 79 defines the test conditions for this test case.

### 968 Table 79 – Write 16-bit Index

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0060

Name TCD_DLIC_ISDU_WRITE16

Purpose (short) 16-bit Write request is possible

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device ISDU test: test to pass

Specification (clause) [1], see 7.3.6.4

Configuration / setup Device-Tester-Unit 969

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends 16-bit ISDU Write request using "Config2". Check whether the Write request was successful. If the Device does not support 16 bit addressing, the access shall be rejected with the correct ErrorCode.

Precondition - Device in PREOPERATE or OPERATE respectively

- DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends 16-bit ISDU Write request using "Config2" b) Receive response "busy" (0x01) until Device is ready c) Receive Write response d) DTU sends ISDU Read request using "Config2" e) Save received data in "temp"

Test parameter "Config2" according to 7.5.1. Manufacturer/vendor is responsible for choosing a variable for the test not changing value after Write.

Post condition – 970

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ISDU response in c)

2. Check received data in e)

Test passed Positive Write response and "temp" = Config2 from IODD or ErrorType = "Index not available"

Test not passed (examples) Negative write response ≠ "Index not available", or "temp" ≠ Config2 from IODD

Report "Config2" changed after Write request: <ok | nok >

971

972

### Test Specification © IO-Link – 121 – Version V1.1.5

### 973 7.5.11 Read 16-bit Index reserved

### 974 Table 80 defines the test conditions for this test case.

### 975 Table 80 – Read 16-bit Index reserved

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0061

Name TCD_DLIC_ISDU_READ16RESERVED

Purpose (short) Device response “Index not available” upon Read to 16-bit reserved Index

Equipment under test (EUT) Device supporting 16-bit Indices

Test case version 1.0

Category / type Device ISDU test: test to fail

Specification (clause) [1], see 7.3.6, and Annex C, Table C.1

Configuration / setup Device-Tester-Unit 976

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends ISDU Read request to Index = 0x7F32 (reserved) and receives a negative response with “Index not available”

Precondition - Device in PREOPERATE or OPERATE respectively

- DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends ISDU Read request to Index 0x7F32, Subindex "0" b) Receive Read response "busy" (0x01) until Device is ready c) Receive Read response when ready

Test parameter –

Post condition – 977

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read response in c)

Test passed Negative Read response (I-Service: 0b1100, Length: 0b0100) with ErrorCode = 0x80, and AdditionalCode = 0x11 “Index not available”

Test not passed (examples) Positive response from Device or incorrect ErrorCode or AdditionalCode

Report Negative Read response upon reserved 16-bit Index: <ok | nok >

978

979

### Version V1.1.5 – 122 – Test Specification © IO-Link

### 980 7.5.12 Read 16-bit Index with unavailable Subindex

### 981 Table 81 defines the test conditions for this test case.

### 982 Table 81 – Read 16-bit Index with unavailable Subindex

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0062

Name TCD_DLIC_ISDU_READ16NOSUBINDEX

Purpose (short) 16-bit Read response when Subindex not available

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device ISDU test: test to fail

Specification (clause) [1], see 7.3.6, and Annex C, Table C.1

Configuration / setup Device-Tester-Unit 983

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends ISDU read request for Config2 with Subindex "0" and Subindex "2" (not implemented). This results in a negative read response depending on Subindex.

Precondition - Device in PREOPERATE or OPERATE respectively

- DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends ISDU Read request with Index in "Config2" and Subindex "0" b) Receive Read response "busy" (0x01) until Device is ready c) Receive and save Read response in "Sub0" d) DTU sends ISDU Read request with Index in Config2 and Subindex = "2" e) Receive Read response "busy" (0x01) until Device is ready f) Receive and save read response in "Sub2"

Test parameter "Config2" in 7.5.1

Post condition – 984

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response combinations

- Sub0 = Sub2 = ErrorType "Index not available"

- Sub0 = positive response AND Sub2 = ErrorType "SubIndex not available"

Test passed If exactly one of the evaluations is correct

Test not passed (examples) None of the evaluations are correct

Report Negative Read response upon unavailable Subindex: <ok | nok >

985

986

### Test Specification © IO-Link – 123 – Version V1.1.5

### 987 7.5.13 Write 8-bit Index with data length overrun

### 988 Table 82 defines the test conditions for this test case.

### 989 Table 82 – Write 8-bit Index with data length overrun

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0063

Name TCD_DLIC_ISDU_WRITE8LENOVERRUN

Purpose (short) Response of 8-bit Write request with 70 octets when 64 are permitted

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device ISDU test: test to fail

Specification (clause) [1], see 7.3.6, and Annex C, Table C.1

Configuration / setup Device-Tester-Unit 990

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends Write request with 70 octets for "Config1" (maximum of 64 octets). Write Request with ExtLength. Check whether the Write request was denied with "Parameter length overrun".

Precondition - Device in PREOPERATE state

- DTU in PREOPERATE state

Procedure a) DTU sends ISDU read request with Config1 b) Receive and save ISDU response in "temp1" c) Master: Send ISDU Write request to Index with Config1 containing 70 octets length and different content from "temp1" d) Receive Write response "busy" (0x01) until Device is ready e) Receive Write response f) DTU sends ISDU Read request with Config1 g) Receive and save ISDU response in "temp2"

Test parameter "Config1" in 7.5.1

Post condition – 991

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ISDU response from e)

2. Check received data from g)

Test passed ErrorType “Parameter length overrun” received "temp1" = "temp2"

Test not passed (examples) Positive Write response or different ErrorType from Device "temp1" ≠ "temp2"

Report Negative Read response upon length overrun: <ok | nok >

992

993

### Version V1.1.5 – 124 – Test Specification © IO-Link

### 994 7.5.14 Write 8-bit Index with data length underrun

### 995 Table 82 defines the test conditions for this test case.

### 996 Table 83 – Write 8-bit Index with with data length underrun

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0064

Name TCD_DLIC_ISDU_WRITE8WRONGLEN

Purpose (short) Response of 8-bit Write request with one octet less than expected

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device ISDU test: test to fail

Specification (clause) [1], see 7.3.6, and Annex C, Table C.1

Configuration / setup Device-Tester-Unit 997

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends Write request with one octet less than the "m" expected octets for Config1. Check whether the Write request was denied with "Parameter length underrun".

Precondition - Device in PREOPERATE state

- DTU in PREOPERATE state

Procedure a) DTU sends ISDU read request with Config1 b) Receive and save ISDU response in "temp1" c) DTU sends ISDU Write request to Index with Config1 containing one octet less length and different content from "temp1" d) Receive Write response "busy" (0x01) until Device is ready e) Receive Write response f) DTU sends ISDU Read request with Config1 g) Receive and save ISDU response in "temp2"

Test parameter "Config1" in 7.5.1

Post condition – 998

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ISDU response in e)

2. Check received data in g)

Test passed ErrorType “Parameter length underrun” received, and "temp1" = "temp2"

Test not passed (examples) Positive Write response or different ErrorType from Device, or "temp1" ≠ "temp2"

Report Negative read response upon length underrun: <ok | nok >

999

1000

### Test Specification © IO-Link – 125 – Version V1.1.5

### 1001 7.5.15 Read 8-bit Index with incorrect Checksum value

### 1002 Table 82 defines the test conditions for this test case.

### 1003 Table 84 – Read 8-bit Index with incorrect Checksum value

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0065

Name TCD_DLIC_ISDU_WRITE8WRONGCHECKSUM

Purpose (short) Response of 8-bit Read request with incorrect checksum value

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device ISDU test: test to fail

Specification (clause) [1], see 7.3.6, Annex A.5, Table A.12, and Table A.14

Configuration / setup Device-Tester-Unit 1004

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends 8-bit Read request with an incorrect Checksum value to get Vendor- Name (0x10). The correct Checksum value is 0x8B, the value in use is 0xFF. A negative Read response "No Service" is expected.

Precondition - Device in PREOPERATE state or OPERATE respectively

- DTU in PREOPERATE state or OPERATE respectively

Procedure a) DTU sends ISDU Read request on Index 0x10 with incorrect checksum 0xFF b) Receive response "busy" (0x01) until Device is ready c) Receive Read response when ready

Test parameter –

Post condition – 1005

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read response in c)

Test passed Negative Read response with I-Service = 0b0000 ("No Service")

Test not passed (examples) Positive response from Device, or ErrorType with not permitted AdditionalCode

Report Read response "No Service" upon incorrect Checksum value: <ok | nok >

1006

1007

### Version V1.1.5 – 126 – Test Specification © IO-Link

### 1008 7.5.16 Write 8-bit Index on read only Index

### 1009 Table 85 defines the test conditions for this test case.

### 1010 Table 85 – Write 8-bit Index on read only Index

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0066

Name TCD_DLIC_ISDU_WRITE8ROINDEX

Purpose (short) Response of 8-bit Write request upon read only Index

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device ISDU test: test to fail

Specification (clause) [1], see 7.3.6, and Annex C, Table C.1; see 10.3.4 and Table 97

Configuration / setup Device-Tester-Unit 1011

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends 8-bit Write request on read only index "VendorName". A negative Write response "Access denied" is expected.

Precondition - Device in PREOPERATE or OPERATE respectively

- DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends ISDU Write request on Index 0x10 with 1 octet data length b) Receive response "busy" (0x01) until Device is ready c) Receive response when ready

Test parameter –

Post condition – 1012

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Write response in c)

Test passed Negative Write response with ErrorCode = 0x80 and AdditionalCode = 0x23 "Access denied"

Test not passed (examples) Negative response ≠ "Access denied", or Positive Write response

Report Negative Write response upon read only Index: <ok | nok >

1013

1014

### Test Specification © IO-Link – 127 – Version V1.1.5

### 1015 7.5.17 Read 8-bit Index with aborted request

### 1016 Table 86 defines the test conditions for this test case.

### 1017 Table 86 – Read 8-bit Index with aborted request

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0067

Name TCD_DLIC_ISDU_ABORTREADREQ

Purpose (short) Response of aborted 8-bit Read request

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device ISDU test: test to pass

Specification (clause) [1], see 7.3.6.4, Figure 52 (T9; for Devices with more than two octets On-request Data within one message: T10 or T11), and Annex A.2, Table A.10

Configuration / setup Device-Tester-Unit 1018

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends and aborts an 8-bit Read request for "VendorName" (0x10). Device switches to Idle mode.

Precondition DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends first ISDU segment in first message b) DTU sends ISDU abort (R, FlowCTRL = ABORT = 0x1F, ISDU) in next message c) DTU sends idle message (R, FlowCTRL = IDLE 1 = 0x11, ISDU)

Test parameter –

Post condition – 1019

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read response

Test passed Read response with I-Service = 0b0000 ("No Service")

Test not passed (examples) Incorrect Device response to incomplete ISDU

Report Device works properly after aborted Read request: <ok | nok >

1020

1021

### Version V1.1.5 – 128 – Test Specification © IO-Link

### 1022 7.5.18 Read 8-bit Index with aborted response

### 1023 Table 87 defines the test conditions for this test case.

### 1024 Table 87 – Read 8-bit Index with aborted response

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0068

Name TCD_DLIC_ISDU_ABORTREADRESP

Purpose (short) Reaction of aborted 8-bit Read response

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device ISDU test: test to pass

Specification (clause) [1], see 7.3.6.4, Figure 52 (T11), Table 50, and Annex A.2, Table A.10

Configuration / setup Device-Tester-Unit 1025

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master sends Read request for VendorName (0x0010) and receives from the Device the expected "VendorName". Master aborts reading the complete "VendorName".

Precondition DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends ISDU Read request to Index 0x0010 (16) b) Receive response "BUSY" (0x01) until Device is ready c) Receive first ISDU segment of the Read response "VendorName" d) DTU sends ISDU abort (R, FlowCTRL = ABORT = 0x1F, ISDU) e) DTU sends idle message (R, FlowCTRL = IDLE 1 = 0x11, ISDU)

Test parameter –

Post condition – 1026

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read response

Test passed Read response with I-Service = 0b0000 ("No Service")

Test not passed (examples) Incorrect Device reply message to the incomplete Read response

Report Device works properly after the aborted Read response: <ok | nok >

1027

1028

### Test Specification © IO-Link – 129 – Version V1.1.5

### 1029 7.5.19 Master retries when ISDU transfer failed

### 1030 Table 88 defines the test conditions for this test case.

### 1031 Table 88 – Master retries when ISDU transfer failed

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0309

Name TCD_DLIC_ISDU_ELEMENT_RETRY

Purpose (short) Behavior of Device if an ISDU segment is being repeated

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device ISDU test: test to pass

Specification (clause) [1], 7.3.6, Table 52

Configuration / setup Device-Tester-Unit 1032

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) If the transmission of a single M-sequence has been corrupted, the Master can send up to three retries requesting the same ISDU segment. Thus, the Device shall repeat the segment and correctly continue the transmission of the ISDU response afterwards. The correct behavior shall be tested sending an ISDU read request using “Config3”. NOTE: If the read result of the ISDU contained in "Config 3" is transmitted in less than three M-sequences, as many segments as possible shall be repeated during the test.

Precondition - Device in PREOPERATE or OPERATE respectively

- DTU in PREOPERATE or OPERATE respectively

Procedure a) DTU sends ISDU Read request using "Config3" b) Receive Read response "busy" (0x01) until Device is ready c) Receive first segment of ISDU Read response;FlowCTRL = 0x10 d) DTU repeats the first segment e) DTU continues reading the next segment;FlowCTRL = 0x01 f) DTU repeats the second segment g) DTU continues reading until the last segment h) DTU repeats the last segment i) DTU sends idle message (R, FlowCTRL=IDLE 1=0x11, ISDU) to finalize transfer j) DTU sends ISDU Read request using "Config3" without retries

Test parameter "Config3" in 7.5.1

Post condition – 1033

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read response in i)

2. Check Length in i)

3. Compare element ExtLength with element COUNT in i)

4. Calculate checksum and compare with saved checksum in i)

5. Compare data in i) and j)

Test passed Positive Read response with I-Service = 0b1101 ("Read response (+)") Length = 0b0001;see ExtLength ExtLength corresponds to COUNT Correct checksum Both data are identical

Test not passed (examples) No, negative or incorrect response from the Device

Report "Config3" received correctly from Device: <ok | nok >

1034

Version V1.1.5 – 130 – Test Specification © IO-Link

1035 7.6 Events

1036 7.6.1 General

1037 Any of the Device applications can generate predefined "status" information when SDCI oper- 1038 ations fail, or "technology specific" information (diagnosis) as a result from technology specific 1039 diagnostic methods. This information can be communicated via SDCI Event to upper level sys- 1040 tems of different capability. Thus, the following tests can only verify the conformity to the SDCI 1041 standard [1] in terms of Event handling mechanisms.

1042 Event test cases shall be executed if the Device supports one or more Events. The Number of 1043 supported events is defined by the number of entries of EventCollection in IODD. Test cases 1044 use Test Events A and B stimulated by ISDU Write to the Index defined in Config7. For each of 1045 the Test Events, Config7 defines a value EventA_Appear/EventB_Appear that stimulates "Event 1046 Appear" and a value EventA_Disappear/EventB_Disappear that stimulates "Event Disappear" 1047 of the corresponding Event.

1048 It is mandatory for a Device to implement the required test Events. Event Codes and Event 1049 types are vendor specific and shall be associated to be a DeviceStautus of 1 to 4. EventCodes 1050 for Event A and Event B shall be different. If the Device supports only Notifications, the test 1051 Events shall be Notifications, and the Event mode is "Event single shot". The Test systems shall 1052 wait 5 seconds after initiating of the event until a timeout is shown. Even if there is no time 1053 specified in [1], the assumption is that an event should not be delayed more than 5 seconds.

1054 Event Test Procedures

1055 In general, a device is allowed to have initial events at the beginning of a test process. It is not allowed 1056 to activate additional unexpected application specific events during the test run.

1057 Event related tests focus on testing of the change of diagnosis during the test run. IO-Link Test Master 1058 reads out the activated events 1s after the precondition is reached and acknowledges all events. 1059 Later-on the IO-Link Test Master only checks for changes in the diagnosis channel.

1060

1061 At the end of each test case, the Device-Tester-Unit shall clean-up the test Events. For stimu- 1062 lated Events, the corresponding EventA_Disappear or EventB_Disappear command shall be 1063 sent, and all Events shall be acknowledged. A pause of 1 s shall be added at the end of the 1064 test to ensure the Event can be stimulated again without delay within the next test case.

1065

### Test Specification © IO-Link – 131 – Version V1.1.5

### 1066 7.6.2 Single Event while in OPERATE state

### 1067 Table 89 defines the test conditions for this test case. For Notification test see 7.5.1.

### 1068 Table 89 – Single Event while in OPERATE state

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0069

Name TCD_DLIC_EVNT_OPERSINGLEEVENT

Purpose (short) Test of single Event processing while in OPERATE state.

Equipment under test (EUT) Device supporting more than one Event

Test case version 1.5

Category / type Device Event test: test to pass

### Specification (clause) [1], see 7.3.8.2

Configuration / setup Device-Tester-Unit 1069

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check if Event flag and Event buffer is serviced as specified:

- Event Flag is raised once an Event occurred

- StatusCode Type 2 is set respectively

- Event page is frozen while Event is pending

- Events are cleared as specified

- Event Type & Code match

Precondition DTU: Communication EUT: OPERATE

Procedure a) Device-Tester to read StatusCode. Save value in tester variable "SCa”. b) Write EventA_Appear to index in Config7 c) Device-Tester to read StatusCode. Save value in tester variable "SCc”. d) Write EventB_Appear to index in Config7 e) Device-Tester to read StatusCode. Save value in tester variable "SCe”. f) Device-Tester to read EventQualifier. g) Device-Tester to read EventCode. h) Device-Tester to write "0xFF" to StatusCode. i) Wait until Event flag is set (again). j) Device-Tester to read StatusCode. k) Device-Tester to read EventQualifier. l) Device-Tester to read EventCode. m) Device-Tester to write "0xFF" to StatusCode. n) Device-Tester to read StatusCode. Save value in tester variable "SCn".

Test parameter Config7 (Event A and B) in 7.5.1

Post condition Test Events A and B are reset once the test is completed

1070

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that Event Flag is set

2. Check after step c) that content of “SCa” and “SCc” are different

3. Check after step c) that “SCc” indicates one Event

4. Check after step e) that content of “SCc” and “SCe” is equal

5. Check after step f) that Event Type equals Event Type of test Event A

6. Check after step g) that Event Code equals Event Code of test Event A

7. Check after step k) that Event Type equals Event Type of test Event B

8. Check after step l) that Event Code equals Event Code of test Event B

9. Check after step m) that Event Flag is cleared

10. Check after step n) that SCn is clear = no events

Test passed All evaluation steps ok.

Test not passed (examples) Any evaluation step failed

Report Deviations in evaluations: <yes/no> <ok | nok> a) Event Flag set: <ok | nok>

### Version V1.1.5 – 132 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

b) Content of "SCa" and "SCc" are different: <ok | nok> c) Content of "SCa": <ok | nok> d) Event Type & Code: <ok | nok> e) Event Flag is cleared: <ok | nok>

1071

1072

### Test Specification © IO-Link – 133 – Version V1.1.5

### 1073 7.6.3 Single Event while in PREOPERATE state

### 1074 Table 90 defines the test conditions for this test case. For Notification test see 7.5.1.

### 1075 Table 90 – Single Event while in PREOPERATE

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0070

Name TCD_DLIC_EVNT_PROPSINGLEEVENT

Purpose (short) Test of single Event processing while in PREOPERATE state

Equipment under test (EUT) Device, supporting more than one Event

Test case version 1.4

Category / type Device Event test: test to pass

Specification (clause) [1], see 7.3.8.2

Configuration / setup Device-Tester-Unit 1076

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check if Event flag and Event buffer is serviced as specified:

- Event flag is raised once Event has occurred

- StatusCode Type 2 is set respectively

- Event page is frozen while event is pending

- Events are cleared as specified

Precondition DTU: Communication EUT: PREOPERATE

Procedure a) Device-Tester to read StatusCode. Save value in tester variable "SCa”. b) Write EventA_Appear to index in Config7 c) Device-Tester to read StatusCode. Save value in tester variable "SCc”. d) Write EventB_Appear to index in Config7 e) Device-Tester to read StatusCode. Save value in tester variable "SCe”. f) Device-Tester to read EventQualifier. g) Device-Tester to read EventCode. h) Device-Tester to write "0xFF" to StatusCode. i) Wait until Event flag is set (again). j) Device-Tester to read StatusCode. k) Device-Tester to read EventQualifier. l) Device-Tester to read EventCode. m) Device-Tester to write "0xFF" to StatusCode. n) Device-Tester to read StatusCode. Save value in tester variable "SCn".

Test parameter Config7 (Event A and B) in 7.5.1

Post condition Test Events A and B are reset once the test is completed 1077

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that Event Flag is set

2. Check after step c) that content of “SCa” and “SCc” are different

3. Check after step c) that “SCc” indicates one Event

4. Check after step e) that content of “SCc” and “SCe” is equal

5. Check after step f) that Event Type equals Event Type of test Event A

6. Check after step g) that Event Code equals Event Code of test Event A

7. Check after step k) that Event Type equals Event Type of test Event B

8. Check after step l) that Event Code equals Event Code of test Event B

9. Check after step m) that Event Flag is cleared

10. Check after step n) that SCn is clear = no events

Test passed All evaluation steps ok.

Test not passed (examples) Any evaluation step failed

### Version V1.1.5 – 134 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

Report Deviations in evaluations: <yes/no> <ok | nok> a) Event Flag set: <ok | nok> b) Content of "SCa" and "SCc" are different: <ok | nok> c) Content of "SCa": <ok | nok> d) Event Type & Code: <ok | nok> e) Event Flag is cleared: <ok | nok>

1078

### Test Specification © IO-Link – 135 – Version V1.1.5

### 1079 7.6.4 Event clearance in OPERATE state

### 1080 Table 91 defines the test conditions for this test case.

### 1081 Table 91 – Event clearance in OPERATE state

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0071

Name TCD_DLIC_EVNT_OPEREVENTCLEAR

Purpose (short) Test of Event clearance while in OPERATE state.

Equipment under test (EUT) Device supporting one or more Events

Test case version 1.4

Category / type Device Event test: test to pass

Specification (clause) [1], see 7.3.8.2

Configuration / setup Device-Tester-Unit 1082

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check if Event flag and Event buffer is serviced as specified:

- Event Flag is raised once an Event occurred

- Events are cleared with any value written back into the StatusCode

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write EventA_Appear to index in Config7. b) Device-Tester to write StatusCode "0x00". c) Wait 50 ms d) Write EventA_Disappear to index in Config7. e) Device-Tester to write StatusCode "0xAA". f) Wait 1 s g) Write EventA_Appear to index in Config7. h) Device-Tester to read StatusCode. Save value in tester variable "SCf”. i) Device-Tester to write StatusCode with value of tester variable "SCf”.

Test parameter Config7 (Event A) in 7.5.1

Post condition Test Events A and B are reset once the test is completed

1083

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that Event Flag is cleared

2. Check after step e) that Event Flag is cleared

3. Check after step i) that Event Flag is cleared

Test passed All evaluation steps ok

Test not passed (examples) Any evaluation step failed

Report Deviations in evaluations: <yes/no> <ok | nok> a) Event Flag in procedure step b: <ok | nok> b) Event Flag in procedure step e: <ok | nok> c) Event Flag in procedure step i: <ok | nok>

1084

1085

### Version V1.1.5 – 136 – Test Specification © IO-Link

### 1086 7.6.5 Event handling while communication interruption

### 1087 Table 92 defines the test conditions for this test case. For Events of type Notification, which

### 1088 usually are not acknowledged, it should be noted that the same rules apply as for Warnings and

### 1089 Errors: The Event shall be resent.

### 1090 Table 92 – Event handling while communication interruption

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0072

Name TCD_DLIC_EVNT_OPERCOMMINTERRUPT

Purpose (short) Test of Event handling while communication is interrupted.

Equipment under test (EUT) Device supporting one or more Events

Test case version 1.5

Category / type Device Event test: test to pass

Specification (clause) [1], see 10.10.2

Configuration / setup Device-Tester-Unit 1091

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check if Event is handled as specified once communication is cancelled or interrupted.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write EventA_Appear to index in Config7 b) DTU to read StatusCode. Save value in tester variable "SCb”. c) DTU to read the indicated EventQualifier ("Event appears"). Save value in tester variable "SCc”. d) DTU to read the indicated EventCode. Save value in tester variable "SCd”. e) DTU performs reset f) Pause of 2 s g) DTU to wake-up Device to OPERATE state h) Read out and acknowledge Events until expected Event occurred (timeout = 15 s)

Test parameter Config7 (Event A) Hint: Messages with transmission errors shall be repeated or dropped.

Post condition Test Events A and B are reset once the test is completed

1092

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step g) that Event Flag is set because the error cause from step a) was not cleared before communication was lost.

2. Check after step h): that one Event received corresponds to tester variable

"SCc", and "SCd".

Test passed All evaluation steps ok

Test not passed (examples) Any evaluation step failed, OR timeout in h)

Report Deviations in evaluations: <yes/no> <ok | nok> a) Event Flag in procedure step g: <ok | nok> b) Read value in step h: <ok | nok>

1093

1094

### Test Specification © IO-Link – 137 – Version V1.1.5

### 1095 7.6.6 Event handling while power supply interruption

### 1096 Table 93 defines the test conditions for this test case. This test case can be skipped in case of

### 1097 Notifications.

### 1098 Table 93 – Event handling while power supply interruption

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0073

Name TCD_DLIC_EVNT_OPERPOWERINTERRUPT

Purpose (short) Test of Event handling while power supply of communication is interrupted.

Equipment under test (EUT) Device supporting Events of type Warning or Error

Test case version 1.5

Category / type Device Event test: test to pass

Specification (clause) [1], see 7.3.8.2

Configuration / setup Device-Tester-Unit 1099

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check if Event modes are handled as specified when power supply of the Device is interrupted: The “Event appears” and “Event disappears” flow must be correct and start with “Event appears” after communication is restarted. Events that are no longer active after communication restart shall not be reported with “Event disappears”.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write EventA_Appear to Index in Config7 b) Device-Tester to read StatusCode c) Device-Tester to read the indicated EventQualifier d) Device-Tester to read the EventCode. Save value in tester variable "SCd” e) Power-down of the Device (disconnect from Device-Tester) f) Pause of 15 sec g) Re-connect Device h) Device-Tester to wake-up Device to OPERATE state i) Read out and acknowledge Events (timeout = 15 s)

Test parameter Config7 (Event A) in 7.5.1

Post condition Test Events A and B are reset once the test is completed 1100

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step d) that one entry of SCd equals EventCode of Event A

2. Check in step i) that Event A is not read with mode “Event disappear”

Test passed All evaluation steps ok, or timeout

Test not passed (examples) Any evaluation step failed

Report Event appears after power off/on cycle: <yes/no> Deviations in evaluations: <yes/no> <ok | nok> a) Read value in step i: <ok | nok>

1101

1102

### Version V1.1.5 – 138 – Test Specification © IO-Link

### 1103 7.6.7 Event appears/disappears

### 1104 Table 94 defines the test conditions for this test case.

### 1105 Table 94 – Event appears/disappears

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0074

Name TCD_DLIC_EVNT_OPERAPPEARDISAPPEAR

Purpose (short) Test of Event handling with Errors appearing and disappearing.

Equipment under test (EUT) Device, supporting one or more Events, test Event is of type Error or Warning

Test case version 1.4

Category / type Device Event test: test to pass

Specification (clause) [1], see 7.3.8.2

Configuration / setup Device-Tester-Unit 1106

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check if Event modes are handled as specified in Figure 11

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write EventA_Appear to Index in Config7 b) Device-Tester to read StatusCode c) Device-Tester to read EventQualifier. Save value in tester variable "SCc”. d) Device-Tester to read EventCode. Save value in tester variable "SCd”. e) Device-Tester to write StatusCode "0xFF". f) Wait 50 ms g) Write EventA_Disappear to Index in Config7 h) Device-Tester to read StatusCode. i) Device-Tester to read EventQualifier. Save value in tester variable "SCh”. j) Device-Tester to read EventCode. Save value in tester variable "SCi”. k) Device-Tester to write StatusCode "0xFF".

Test parameter Config7 (Event A) in 7.5.1

Post condition Test Events A and B are reset once the test is completed

1107

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step a) that Event Flag is set

2. Check after step c) that value read shows mode = "Event appeared"

3. Check after step e) that Event Flag is cleared

4. Check after step g) that Event Flag is set

5. Check after step i) that value read shows mode = "Event disappeared"

6. Check after step j) that value of "SCi" equals value of "SCd" (EventCodes).

7. Check after step k) that Event Flag is cleared

Test passed All evaluation steps ok

Test not passed (examples) Any evaluation step failed

Report Deviations in evaluations: <yes/no> <ok | nok> a) Event Flag in procedure step a: <ok | nok> b) Value in procedure step c: <ok | nok> c) Event Flag in procedure step e: <ok | nok> d) Event Flag in procedure step g: <ok | nok> e) Value in procedure step i: <ok | nok> f) EventCodes in procedure step j: <ok | nok> g) Event Flag in procedure step k: <ok | nok>

1108

1109

### Test Specification © IO-Link – 139 – Version V1.1.5

### 1110 Figure 11 shows the relationship of an Error and the Event Flag and its appearance and disap-

### 1111 pearance.

1112

### 1113 Figure 11 – Relationship of an Error and the Event Flag

1114

Error

Event Flag

Error appeared Error disappeared

t SCSC SCSC

Device-Tester writes StatusCode

### Version V1.1.5 – 140 – Test Specification © IO-Link

### 1115 7.6.8 Multi Event handling

### 1116 Table 95 defines the test conditions for this test case.

### 1117 Table 95 – Multi Event handling

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0075

Name TCD_DLIC_EVNT_OPERMULTEVENT

Purpose (short) Test of Event handling with multiple Events.

Equipment under test (EUT) Device supporting more than one Event

Test case version 1.5

Category / type Device Event test: test to pass

Specification (clause) [1], see 7.3.8.2

Configuration / setup Device-Tester-Unit 1118

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check if Event modes are handled as specified in Figure 12

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write EventA_Appear to index in Config7 b) Device-Tester to read StatusCode c) Device-Tester to read EventQualifier. Save value in tester variable "SCc”. d) Device-Tester to read EventCode. Save value in tester variable "SCd”. e) Device-Tester to write Status Code "0xFF". f) Write EventB_Appear to index in Config7 g) Device-Tester to read StatusCode h) Wait 50 ms i) Write EventA_Disappear to index in Config7 j) Device-Tester to read EventQualifier. Save value in tester variable "SCj”. k) Device-Tester to read EventCode. Save value in tester variable "SCk”. l) Device-Tester to write Status Code "0xFF". m) Device-Tester to read StatusCode n) Write EventB_Disappear to index in Config7 o) Device-Tester to read EventQualifier. Save value in tester variable "SCo”. p) Device-Tester to read EventCode. Save value in tester variable "SCp”. q) Device-Tester to write StatusCode "0xFF". r) Device-Tester to read StatusCode s) Device-Tester to read EventQualifier. Save value in tester variable "SCs”. t) Device-Tester to read EventCode. Save value in tester variable "SCt”. u) Device-Tester to write StatusCode "0xFF".

Test parameter Config7 (Event A and B) in 7.5.1

Post condition Test Events A and B are reset once the test is completed

1119

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step c) that value read shows mode = "Event appeared" or "Event single shot” in case of Notification

2. Check after step d) that SCd equals EventCode of Event A

3. Check after step d) that Event Flag is set

4. Check after step e) that Event Flag is cleared

5. Check after step f) that Event Flag is set

6. Check after step j) that value read shows mode = "Event appeared" or

"Event single shot" in case of Notification

7. Check after step k) that SCk equals EventCode of Event B

8. Check after step k) that Event Flag is set

9. Check after step o) that value read shows mode = "Event disappeared" or

"Event single shot" in case of Notification

10. Check after step p) that SCp equals EventCode of Event A

11. Check after step p) that Event Flag is set

### Test Specification © IO-Link – 141 – Version V1.1.5

12. Check after step s) that value read shows mode = "Event disappeared" or

"Event single shot" in case of Notification

13. Check after step t) that SCt equals EventCode of Event B

14. Check after step t) that Event Flag is set

15. Check after step u) that Event Flag is cleared

Test passed All evaluation steps ok

Test not passed (examples) Any evaluation step failed

Report Deviations in evaluations: <yes/no> <ok | nok>

1120

### 1121 Figure 12 shows the correlation of two Errors and the Event Flag and its appearance and dis-

### 1122 appearance. In case of Notifications the rising and falling edges define the time the Event is

### 1123 stimulated.

1124

### 1125 Figure 12 – Correlation of two Errors and the Event Flag

1126

Error A

Event Flag

A appeared B appeared

t

Error B

A disappeared B disappeared

SC SC SC SC

Device-Tester writes StatusCode

### Version V1.1.5 – 142 – Test Specification © IO-Link

### 1127 7.6.9 Short time Events

### 1128 Table 96 defines the test conditions for this test case.

### 1129 Table 96 – Short time Events

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0076

Name TCD_DLIC_EVNT_OPERSHORTEVENT

Purpose (short) Test of the Event handling of short time errors.

Equipment under test (EUT) Device, supporting one or more Events, test Event is of type Error or Warning

Test case version 1.3

Category / type Device Event test: test to pass

Specification (clause) [1], see 7.3.8.2

Configuration / setup Device-Tester-Unit 1130

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Event handling of short time Errors according Figure 13. "Short time Error" means that the cause of the Error is no more existent, when the acknowledgement from the Master for the appearance occurred (Write access to StatusCode (SC)). The Device shall send "Event disappeared" in this case after the acknowledgement.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write EventA_Appear to index in Config7. b) Write EventA_Disappear to index in Config7 c) Device-Tester to write StatusCode "0xFF". d) Wait for 50 ms or two times MasterCycleTime whichever is longer e) Device-Tester to write StatusCode "0xFF"

Test parameter Config7 (Event A) in 7.5.1 and MasterCycleTime

Post condition Test Events A and B are reset once the test is completed

1131

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step a) that Event Flag is set

2. Check after step d) that Event Flag is set

3. Check after step e) that Event Flag is cleared

Test passed All evaluation steps ok

Test not passed (examples) Any evaluation step failed

Report Deviations in evaluations: <yes/no> <ok | nok> Event has been latched: <ok | nok>

1132

### 1133 Figure 13 shows the timings of a short time error and the Event flag.

### Test Specification © IO-Link – 143 – Version V1.1.5

1134

### 1135 Figure 13 – Timings of a short time Error and the Event flag

1136

Error

Event Flag

Error appeared Error disappeared

t SC SC

Device-Tester writes StatusCode

T = x

T > x

### Version V1.1.5 – 144 – Test Specification © IO-Link

### 1137 7.6.10 Interconnection active Event/Device Status

### 1138 Table 97 defines the test conditions for this test case.

### 1139 Table 97 – Interconnection active Event/Device Status/Detailed Device Status

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0373

Name TCD_DLIC_DEFP_EVENTDEVSTAT

Purpose (short) Check correct interconnection between Event and (detailed) Device Status

Equipment under test (EUT) Device supporting Events of type Warning or Error And Parameters Device Status and Detailed Device Status

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.20 and B.2.21 and Table D.1

Configuration / setup Device-Tester-Unit 1140

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test correct behavior regarding the interconnection of an active Event and the resulting content of parameter Device Status and Detailed Device Status.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read Parameter Device Status and store value in “devStatBase” b) Read Parameter Detailed Device Status, parse result from beginning to end in steps of 3 octets and store values in “detDevStatBase” c) Write EventA_Appear to index in Config7 d) Read Parameter Device Status e) Read Parameter Detailed Device Status, parse result from beginning to end in steps of 3 octets f) Write EventA_Disappear to index in Config7 g) Read Parameter Device Status h) Read Parameter Detailed Device Status, parse result from beginning to end in steps of 3 octets

Test parameter –

Post condition – 1141

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check values at step d)

2. Check values at step e)

3. Check values at step g)

4. Check values at step h)

Test passed 1) Device Status = 1 (maintenance required) to 4 (failure)

2. Check Detailed Device Status: One entry must be the Event Code given in Con-

fig 7

3. Device Status must be the same as stored in a) (“devStatBase”)

4. Detailed Device Status Entry given in config 7 has been deleted, content

matches stored values in b) (“detDevStatBase”)

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

1142

Test Specification © IO-Link – 145 – Version V1.1.5

1143 7.7 Data Storage (DS)

1144 7.7.1 General

1145 7.7.1.1 Checks on Data Storage Index

1146 Checks on different states/values shall be performed according to the notes in the "Evaluation" 1147 and "Report" field of the test cases:

1148 • State_Property 1149 - value of "State of Data Storage" 1150 - value of "DS_UPLOAD_FLAG"

1151 • Data_Storage_Size 1152 - shall be larger or equal to the memory size of the objects in the Index List, as described 1153 in "Structure of the stored DS data objects". Note: Data_Storage_Size can be larger in 1154 case Strings are transferred in condensed format. 1155 - check after Upload

1156 • Parameter_checksum 1157 - This value shall be changed after modification of parameters listed for data storage 1158 - Check after parameter modification

1159 These states are specified in [1], 10.4.2 (Data Storage state machine), and B.2.3 (Data Storage 1160 Index).

1161 "Parameter set 1" and "Parameter set 2" are used as placeholders for two parameter sets ful- 1162 filling the following conditions:

1163 • "Parameter set 1" and "Parameter set 2" contain parameters listed for data storage

1164 • "Parameter set 1" and "Parameter set 2" are different in parameter values listed for data 1165 storage

1166 • "Parameter set 1" and the parameter set of the delivered Device are different in values

1167 7.7.1.2 Generation of "DS_UPLOAD_REQ"

1168 It would be possible to test the generation of "DS_UPLOAD_REQ" in separate test cases. But 1169 these tests are already performed within the test cases for Upload and Download.

1170 7.7.1.3 Different Upload test cases

1171 Upload is tested in different states of the Device.

1172 7.7.1.4 Different Download test cases

1173 Download is only checked with "DS_UPLOAD_REQ" flag = "0" (false).

1174

### Version V1.1.5 – 146 – Test Specification © IO-Link

### 1175 7.7.2 Upload without DS_UPLOAD_FLAG notification

### 1176 Table 98 defines the test conditions for this test case.

### 1177 Table 98 – Upload without DS_UPLOAD_FLAG notification

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0077

Name TCD_APPS_DSUP_NOFLAG

Purpose (short) Explicit upload without DS_UPLOAD_FLAG notification

Equipment under test (EUT) Device with Datastorage support

Test case version 1.1

Category / type Device DS test: test to pass

Specification (clause) [1], see 10.4.2, Table B.11, Figure 90

Configuration / setup Device-Tester-Unit 1178

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test covers upload of Data Storage contents (parameter set) without DS_UP- LOAD_FLAG notification

Precondition - Device in PREOPERATE or OPERATE mode

- DS_UPLOAD_FLAG is not set

- Device parameterized (manufacturer to define parameter set)

Procedure Perform upload completely as defined in DTU DS state machine: a) Switch DTU DS from deactivated to activated state. b) Stimulate upload using DS_Commands "DS_UploadStart" and "DS_UploadEnd"

Test parameter Parameter set (manufacturer to define parameter set)

Post condition – 1179

TEST CASE RESULTS CHECK / REACTION

Evaluation Check whether parameter set is read without errors through Data Storage Index. Upon each of the following actions:

1. After call of the DS_UploadStart command

2. After reading/uploading the parameters

3. After call of the DS_UploadEnd command

check the following:

4. State of Data Storage is correct

5. DS_UPLOAD_FLAG is not set

6. Parameter_checksum does not change

7. Verify that the value of DS Size is larger or equal with to the size of the up-

loaded data + 4 * number of entries in Index List (see IO-Link System Spec. Annex G)

st passed All three checks during the three actions described in evaluation are positive

Test not passed (examples) Any check in evaluation failed

Report Result of evaluation action 1) and check 4): <state> <ok | nok> Result of evaluation action 1) and check 5): <flag> <ok | nok> Result of evaluation action 1) and check 6: <checksum> <ok | nok> Result of evaluation action 2) and check 4): <state> <ok | nok> Result of evaluation action 2) and check 5): <flag> <ok | nok> Result of evaluation action 2) and check 6): <checksum> <ok | nok> Result of evaluation action 3) and check 4): <state> <ok | nok> Result of evaluation action 3) and check 5): <flag> <ok | nok> Result of evaluation action 3) and check 6): <checksum> <ok | nok>

1180

1181

### Test Specification © IO-Link – 147 – Version V1.1.5

### 1182 7.7.3 Upload via ParamDownloadStore

### 1183 Table 99 defines the test conditions for this test case.

### 1184 Table 99 – Upload via ParamDownloadStore

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0078

Name TCD_APPS_DSUP_VIADOWNLOADSTORE

Purpose (short) Explicit upload via SystemCommand "ParamDownloadStore"

Equipment under test (EUT) Device with Datastorage support

Test case version 1.1

Category / type Device DS test: test to pass

Specification (clause) [1], see 10.4.2, Tables B.8, B.9, B.11, and D.1, Figure 90

Configuration / setup Device-Tester-Unit 1185

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test covers parameterization (parameter set 2) of a Device and upload of this set into the Master DS per SystemCommand "ParamDownloadStore". Manufacturer is responsible for the definition of two possible "parameter sets".

Precondition - Device in PREOPERATE or OPERATE mode

- Parameter set 1 stored within Device (manufacturer to define parameter set 1)

- DS_UPLOAD_FLAG is not set

- Test preparation e.g. using DS_Command "DS_UploadStart", upload of

parameter set 1" using the Index List (upload), followed by a "DS_UploadEnd"

Procedure a) Call SystemCommand "ParamDownloadStart" if Device supports Block Parameterization b) Write different parameter set 2 into the Device c) Call SystemCommand "ParamDownloadStore" (causes Event DS_UPLOAD_REQ) d) Wait for event DS_UPLOAD_REQ e) Perform Upload (Data Storage) completely as defined in the Master state machine (switch Master DS from deactivated to activated)

Test parameter Parameter set 1 and parameter set 2 (defined by manufacturer)

Post condition – 1186

TEST CASE RESULTS CHECK / REACTION

Evaluation Check whether parameter set 2 is read without errors through Parameter_checksum. Upon each of the following actions:

1. After call of the DS_UploadStart command

2. After reading/uploading the parameters

3. After call of the DS_UploadEnd command

check the following:

4. State of Data Storage is correct (active after DS_UploadStart, inactive after

DS_UploadEnd)

5. DS_UPLOAD_FLAG is correct (set after “ParamDownloadStore” and cleared af-

ter DS_UploadEnd)

6. Parameter_checksum has changed to that of parameter set 2

Test passed All three checks during the three actions described in evaluation are positive

Test not passed (examples) Any check in evaluation failed

Report Result of evaluation action 1) and check 4): <state> <ok | nok> Result of evaluation action 1) and check 5): <flag> <ok | nok> Result of evaluation action 1) and check 6): <checksum> <ok | nok> Result of evaluation action 2) and check 4): <state> <ok | nok> Result of evaluation action 2) and check 5): <flag> <ok | nok> Result of evaluation action 2) and check 6): <checksum> <ok | nok> Result of evaluation action 3) and check 4): <state> <ok | nok> Result of evaluation action 3) and check 5): <flag> <ok | nok> Result of evaluation action 3) and check 6): <checksum> <ok | nok>

1187

1188

### Version V1.1.5 – 148 – Test Specification © IO-Link

### 1189 7.7.4 Upload via ParamDownloadStore without write calls

### 1190 Table 100 defines the test conditions for this test case.

### 1191 Table 100 – Upload via ParamDownloadStore without write calls

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0079

Name TCD_APPS_DSUP_VIADOWNLOADSTORENOWRITE

Purpose (short) Explicit upload via "ParamDownloadStore" without write calls

Equipment under test (EUT) Device with Datastorage support

Test case version 1.2

Category / type Device DS test: test to pass

Specification (clause) [1], see 10.4.2, Tables B.8, B.11, and D.1, Figure 90

Configuration / setup Device-Tester-Unit 1192

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test covers parameterization (parameter set 2) of a Device and upload of this set into the Master DS per SystemCommand "ParamDownloadStore". Manufacturer is responsible for the definition of two possible "parameter sets".

Precondition - Device in PREOPERATE or OPERATE mode

- Parameter set 1 stored within Device (manufacturer to define parameter set 1)

- DS_UPLOAD_FLAG is not set

- Test preparation e.g. using DS_Command "DS_UploadStart", upload of

parameter set 1" using the Index List (upload), followed by a "DS_UploadEnd"

Procedure a) Write different parameter set 2 into the Device b) Wait 1 s for event DS_UPLOAD_REQ c) Call SystemCommand "ParamDownloadStart" via ISDU (w/o writing parameters), if Device supports Block Parameterization d) Initiate upload via SystemCommand "ParamDownloadStore" e) Wait for Event DS_UPLOAD_REQ f) Perform upload (Data Storage) completely as defined in the Master state machine (switch Master DS from deactivated to activated)

Test parameter Parameter set 1 and 2 (manufacturer to define possible parameter sets)

Post condition – 1193

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether parameter set 2 is written without errors

2. Check whether Event DS_UPLOAD_REQ was not raised after changing

parameters (parameter set 2)

3. Check whether Event DS_UPLOAD_REQ was raised by the Device after

SystemCommand "ParamDownloadStore"

4. Check whether parameter set 2 is read without errors through

Parameter_checksum Upon each of the following actions:

5. After call of the DS_UploadStart command

6. After reading/uploading the parameters

7. After call of the DS_UploadEnd command

check via Data Storage Index the following:

8. State of Data Storage is correct

9. DS_UPLOAD_FLAG is correct(set after “ParamDownloadStore” and cleared af-

ter DS_UploadEnd)

10. Parameter_checksum has changed only after "Write parameter set 2"

Test passed All checks in 1) to 4) and all three checks during the three actions described in evaluation are positive

Test not passed (examples) Any check in evaluation failed

Report Result of evaluation 1): <checksum> <ok | nok> Result of evaluation 2): <flag> <ok | nok> Result of evaluation 3): <flag> <ok | nok> Result of evaluation 4): <checksum> <ok | nok> Result of evaluation action 5) and check 8): <state> <ok | nok>

### Test Specification © IO-Link – 149 – Version V1.1.5

TEST CASE RESULTS CHECK / REACTION Result of evaluation action 5) and check 9): <flag> <ok | nok> Result of evaluation action 5) and check 10): <checksum> <ok | nok> Result of evaluation action 6) and check 8): <state> <ok | nok> Result of evaluation action 6) and check 9): <flag> <ok | nok> Result of evaluation action 6) and check 10): <checksum> <ok | nok> Result of evaluation action 7) and check 8): <state> <ok | nok> Result of evaluation action 7) and check 9): <flag> <ok | nok> Result of evaluation action 7) and check 10): <checksum> <ok | nok>

1194

1195

### Version V1.1.5 – 150 – Test Specification © IO-Link

### 1196 7.7.5 Upload via local parameter modification

### 1197 Table 101 defines the test conditions for this test case.

### 1198 Table 101 – Upload via local parameter modification

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0080

Name TCD_APPS_DSUP_VIALOCALCHANGE

Purpose (short) Implicit upload after local parameter modification

Equipment under test (EUT) Device with local parameterization such as teach-in or panel as indicated in IODD and with Datastorage support

Test case version 1.1

Category / type Device DS test: test to pass

Specification (clause) [1], see 10.4.2, Tables B.8, B.11, and D.1, Figure 90

Configuration / setup Device-Tester-Unit 1199

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test covers local parameter changes of a Device and implicit upload of the parameter set into the Master DS. Manufacturer is responsible for the definition of two possible "parameter sets".

Precondition - Device in PREOPERATE or OPERATE mode

- Parameter set 1 stored within Device (manufacturer to define parameter set 1)

- DS_UPLOAD_FLAG is not set

- Test preparation e.g. using DS_Command "DS_UploadStart", upload of

parameter set 1" using the Index List (upload), followed by a "DS_UploadEnd"

Procedure a) Change parameter value(s) locally in the Device, e.g. via local menu or teach-in b) Wait for Event DS_UPLOAD_REQ c) Perform upload (Data Storage) completely as defined in the Master state machine (switch Master DS from deactivated to activated)

Test parameter Parameter set 1 (manufacturer to define the possible parameter set)

Post condition – 1200

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether Event DS_UPLOAD_REQ was raised automatically by the Device after local parameter modification

2. Check whether parameter set 2 is read without errors through

Parameter_checksum Upon each of the following actions:

3. After call of the DS_UploadStart command

4. After reading/uploading the parameters

5. After call of the DS_UploadEnd command

check via Data Storage Index the following:

6. State of Data Storage is correct

7. DS_UPLOAD_FLAG is correct (set after local change of parameter values and

cleared after DS_UploadEnd)

8. Parameter_checksum has changed only after 'local change of parameter values'

Test passed All checks in 1), 2), and all three checks during the three actions described in evaluation are positive

Test not passed (examples) Any check in evaluation failed

Report Result of evaluation 1): <flag> <ok | nok> Result of evaluation 2): <checksum> <ok | nok> Result of evaluation action 3) and check 6): <state> <ok | nok> Result of evaluation action 3) and check 7): <flag> <ok | nok> Result of evaluation action 3) and check 8): <checksum> <ok | nok> Result of evaluation action 4) and check 6): <state> <ok | nok> Result of evaluation action 4) and check 7): <flag> <ok | nok> Result of evaluation action 4) and check 8): <checksum> <ok | nok> Result of evaluation action 5) and check 6): <state> <ok | nok> Result of evaluation action 5) and check 7): <flag> <ok | nok> Result of evaluation action 5) and check 8): <checksum> <ok | nok>

Test Specification © IO-Link – 151 – Version V1.1.5

1201

### Version V1.1.5 – 152 – Test Specification © IO-Link

### 1202 7.7.6 Call ParamBreak in different states of Upload

### 1203 Table 102 defines the test conditions for this test case.

### 1204 Table 102 – Call ParamBreak in different states of Upload

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0081

Name TCD_APPS_DSUP_PARABREAKABORT

Purpose (short) Upload abort via SystemCommand "ParamBreak" in different states

Equipment under test (EUT) Device with Datastorage and Block Parameterization support

Test case version 1.3

Category / type Device DS test: test to pass

Specification (clause) [1], see 10.4.2, Tables B.8, B.11, and D.1, Figure 90

Configuration / setup Device-Tester-Unit 1205

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test covers Upload aborts via SystemCommand "ParamBreak" in different states. Manufacturer is responsible for the definition of two possible "parameter sets".

Precondition - Device in PREOPERATE or OPERATE mode

- Parameter set 1 stored within Device (manufacturer to define parameter set 1)

- DS_UPLOAD_FLAG is not set

- Test preparation e.g. using DS_Command "DS_UploadStart", upload of

parameter set 1" using the Index List (upload), followed by a "DS_UploadEnd"

Procedure a) Call SystemCommand "ParamDownloadStart" b) Write different parameter set 2 into the Device c) Call SystemCommand "ParamDownloadStore" (causes Event DS_UPLOAD_REQ) d) Wait for Event DS_UPLOAD_REQ e) Start Upload via SystemCommand "ParamUploadStart" f) Call SystemCommand "ParamBreak" directly after "ParamUploadStart" g) Start Upload via SystemCommand "ParamUploadStart" h) Transmit first Block Parameter object of Config4 i) Call SystemCommand "ParamBreak" j) Start Upload via SystemCommand "ParamUploadStart" k) Transmit all Block Parameter objects of Config4 l) Call SystemCommand "ParamBreak" m) Perform upload (Data Storage) completely as defined in the Master state machine (switch Master DS from deactivated to activated)

Test parameter Parameter set 1 and 2

Post condition – 1206

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether Event DS_UPLOAD_REQ was raised automatically by the Device

2. Check whether parameter set 2 is read without errors through

Parameter_checksum Upon each of the following actions:

3. After call of each SystemCommand "ParamUploadStart" (3x)

4. After call of each SystemCommand "ParamBreak" (3x)

check via Data Storage Index the following:

5. State of Data Storage is 0b00 (inactive)

6. DS_UPLOAD_FLAG is set

7. Parameter_checksum has changed only after 'write parameter set 2'

8. Check whether the Upload has been completed without errors

Test passed All checks in 1), 2), 8), and all three checks during the two actions described in evaluation are positive

Test not passed (examples) Any check in evaluation failed

Report Result of evaluation 1): <flag> <ok | nok> Result of evaluation 2): <checksum> <ok | nok> Result of evaluation action 3) and check 5): <state> <ok | nok> Result of evaluation action 3) and check 6): <flag> <ok | nok> Result of evaluation action 3) and check 7): <checksum> <ok | nok>

### Test Specification © IO-Link – 153 – Version V1.1.5

TEST CASE RESULTS CHECK / REACTION Result of evaluation action 4) and check 5): <state> <ok | nok> Result of evaluation action 4) and check 6): <flag> <ok | nok> Result of evaluation action 4) and check 7): <checksum> <ok | nok> Result of evaluation 8): <ok | nok> 1207

### Version V1.1.5 – 154 – Test Specification © IO-Link

### 1208 7.7.7 Check structure of Data Storage Index List

### 1209 Table 103 defines the test conditions for this test case.

### 1210 Table 103 – Check structure of Data Storage Index List

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0378

Name TCD_APPS_DSUP_INDEXLIST

Purpose (short) Check structure of Data Storage Index List

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device DS test: test to pass

Specification (clause) B.2.3, Table B.11

Configuration / setup Device-Tester-Unit 1211

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check structure of Index_List and concatenated Index_Lists if available

Precondition DTU: Communication EUT: PREOPERATE

Procedure a) Read Index 3, extract Index_List (Subindex 5) b) Store length of Index_List (number of octets) in <len> c) Calculate and store <EntryCount> = (<len>-2 ) / 3 d) Store termination marker in <term> e) If <term> is not equal 0x00 0x00 read next Index_List from index referenced by <term> and repeat from step b)

Input parameter

Post condition - 1212

TEST CASE RESULTS CHECK / REACTION

Evaluation For each loop cycle check:

1. After step c) check: <EntryCount> must be an integer value in range 0 to 70

Test passed All evaluations with positive result

Test failed (examples) Any evaluation failed

Report All evaluations <ok | nok>

1213

### Test Specification © IO-Link – 155 – Version V1.1.5

### 1214 7.7.8 Download after modification of parameters

### 1215 Table 104 defines the test conditions for this test case.

### 1216 Table 104 – Download after modification of parameters

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0082

Name TCD_APPS_DSDN_PARAMODIFICATION

Purpose (short) Download after modification of parameters

Equipment under test (EUT) Device with Datastorage and Block Parameterization support

Test case version 1.2

Category / type Device DS test: test to pass

Specification (clause) [1], see 10.4.2, Tables B.8, B.11, and D.1, Figure 90

Configuration / setup Device-Tester-Unit 1217

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) After changing parameters in Device without storing them in DS, download a different parameter set into the Device.

Precondition - Device in PREOPERATE or OPERATE mode

- Parameter set 1 stored within Device (manufacturer to define parameter set 1)

- DS_UPLOAD_FLAG is not set

- Test preparation e.g. using DS_Command "DS_UploadStart", upload of

parameter set 1" using the Index List (upload), followed by a "DS_UploadEnd"

Procedure a) Call SystemCommand "ParamDownloadStart" via ISDU b) Write different parameter set 2 into the Device c) Call SystemCommand "ParamDownloadEnd" d) Perform Download (restore) of parameter set 1 completely as defined in the Master state machine only if the DS_UPLOAD_FLAG is not set (switch Master DS from deactivated to activated)

Test parameter Parameter set 1 and 2

Post condition – 1218

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether Event DS_UPLOAD_REQ was not raised by the Device Upon each of the following actions:

2. After call of SystemCommand "ParamDownloadStart"

3. After call of SystemCommand "ParamDownloadEnd"

4. After execution of the complete Download

check via Data Storage Index the following:

5. State of Data Storage is 0b00 (inactive)

6. DS_UPLOAD_FLAG is not set

7. Parameter_checksum has changed only after 'write parameter set 2'

8. Check whether the Download has been completed without errors

Test passed All checks in 1), 8), and all three checks during the three actions described in evaluation are positive

Test not passed (examples) Any check in evaluation failed

Report Result of evaluation 1): <flag> <ok | nok> Result of evaluation action 2) and check 5): <state> <ok | nok> Result of evaluation action 2) and check 6): <flag> <ok | nok> Result of evaluation action 2) and check 7): <checksum> <ok | nok> Result of evaluation action 3) and check 5): <state> <ok | nok> Result of evaluation action 3) and check 6): <flag> <ok | nok> Result of evaluation action 3) and check 7): <checksum> <ok | nok> Result of evaluation action 4) and check 5): <state> <ok | nok> Result of evaluation action 4) and check 6): <flag> <ok | nok> Result of evaluation action 4) and check 7): <checksum> <ok | nok> Result of evaluation 8): <ok | nok>

1219

1220

### Version V1.1.5 – 156 – Test Specification © IO-Link

### 1221 7.7.9 Download into the Device after reset

### 1222 Table 105 defines the test conditions for this test case.

### 1223 Table 105 – Download into the Device after reset

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0083

Name TCD_APPS_DSDN_FACTORYRESET

Purpose (short) Download into the Device after reset to factory settings

Equipment under test (EUT) Device with "Reset to factory settings" support and with Datastorage support

Test case version 1.0

Category / type Device DS test: test to pass

Specification (clause) [1], see 10.4.2, 10.7, Tables B.8, B.11, and D.1, Figure 90

Configuration / setup Device-Tester-Unit 1224

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test of Download of the stored parameter set into the Device after reset to factory settings. Manufacturer to provide parameter set 1 different to factory settings.

Precondition - Device in PREOPERATE or OPERATE mode

- Parameter set 1 stored within Device (manufacturer to define parameter set 1)

- DS_UPLOAD_FLAG is not set

- Test preparation e.g. using DS_Command "DS_UploadStart", upload of

parameter set 1" using the Index List (upload), followed by a "DS_UploadEnd"

Procedure a) Call SystemCommand "Restore factory settings" via ISDU b) Perform Download (restore) of parameter set 1 completely as defined in the Master state machine only if the DS_UPLOAD_FLAG is not set (switch Master DS from deactivated to activated)

Test parameter Parameter set 1

Post condition – 1225

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After "Restore factory settings" check whether Event DS_UPLOAD_REQ was not raised by the Device. Upon the following action:

2. After "Restore factory settings"

check via Data Storage Index the following:

3. State of Data Storage is correct

4. DS_UPLOAD_FLAG is not set

5. Parameter_checksum has changed

6. Check whether the Download has been completed without errors

Test passed All checks in 1), 6), and all three checks during the action described in evaluation are positive

Test not passed (examples) Any check in evaluation failed

Report Result of evaluation 1): <flag> <ok | nok> Result of evaluation action 2) and check 3): <state> <ok | nok> Result of evaluation action 2) and check 4): <flag> <ok | nok> Result of evaluation action 2) and check 5): <checksum> <ok | nok> Result of evaluation 6): <ok | nok>

1226

1227

### Test Specification © IO-Link – 157 – Version V1.1.5

### 1228 7.7.10 Call ParamBreak in different states of Download

### 1229 Table 106 defines the test conditions for this test case.

### 1230 Table 106 – Call ParamBreak in different states of Download

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0084

Name TCD_APPS_DSDN_PARABREAKABORT

Purpose (short) Download abort via SystemCommand "ParamBreak" in different states

Equipment under test (EUT) Device with Datastorage and Block Parameterization support

Test case version 1.3

Category / type Device DS test: test to pass

Specification (clause) [1], see 10.4.2, Tables B.8, B.11, and D.1, Figure 90

Configuration / setup Device-Tester-Unit 1231

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test covers Download aborts via SystemCommand "ParamBreak" in different states. Manufacturer is responsible for the definition of two possible "parameter sets".

Precondition - Device in PREOPERATE or OPERATE mode

- Parameter set 1 stored within Device (manufacturer to define parameter set 1)

- DS_UPLOAD_FLAG is not set

- Test preparation e.g. using DS_Command "DS_UploadStart", upload of

parameter set 1" using the Index List (upload), followed by a "DS_UploadEnd"

Procedure a) Start Download via SystemCommand "ParamDownloadStart" b) Call SystemCommand "ParamBreak" directly after "ParamDownloadStart" c) Start Download via SystemCommand "ParamDownloadStart" d) Transmit first Block Parameter object of Config4 with data of parameter set 2 e) Call SystemCommand "ParamBreak" f) Start Download via SystemCommand "ParamDownloadStart" g) Transmit all Block Parameter objects of Config4 with data of parameter set 2 h) Call SystemCommand "ParamBreak" i) Perform Download of parameter set 2 completely as defined in the Master state machine only if the DS_UPLOAD_FLAG is not set in the Device (switch Master DS from deactivated to activated)

Test parameter Parameter set 1 and 2

Post condition – 1232

TEST CASE RESULTS CHECK / REACTION

Evaluation After call of each SystemCommand "ParamBreak" (3x) check via Data Storage Index the following:

1. State of Data Storage is 0b00 (inactive)

2. DS_UPLOAD_FLAG is not set

3. Parameter_checksum has not changed

After i):

4. Check whether the Download has been completed without errors

Test passed All checks during the actions described in evaluation are positive

Test not passed (examples) Any check in evaluation failed

Report All evaluations: <ok | nok>

1233

### Version V1.1.5 – 158 – Test Specification © IO-Link

### 1234 7.7.11 Parameter Manager – Single Parameter and DownloadStore

### 1235 Table 107 defines the test conditions for this test case.

### 1236 Table 107 – Parameter Manager – Single Parameter and DownloadStore

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0321

Name TCD_DSBP_APPL_DSSINGLEPARAM

Purpose (short) Test of single parameter write in Parameter Manager Idle

Equipment under test (EUT) Device with Data Storage as indicated in IODD

Test case version 1.0

Category / type Device DS test: test to pass

Specification (clause) [1], 10.3.2 and 10.3.4

Configuration / setup Device-Tester-Unit 1237

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device shall not invoke DataStorage if parametrized via single parameter accesses. Device shall invoke DataStorage after receiving a SystemCommand "ParamDownloadStore".

Precondition DTU: Communication EUT: No Event pending or active DataStorage of the Device is inactive and DS_UPLOAD_FLAG is "0" Procedure a) Write test parameter with different content than stored in Device b) Wait for DS_UPLOAD_REQ Event with a timeout of 1 s c) Read DataStorage state property: Index 3, Subindex 2 d) Write SystemCommand "ParamDownloadStore" e) Wait for DS_UPLOAD_REQ Event with a timeout of 1 s f) Read Data Storage state property: Index 3, Subindex 2 g) Write DS_Command "DS_UploadEnd"

Test parameter One parameter of the parameter sets defined in 7.7.1.1

Post condition – 1238

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After step b), check that timeout occurred and no Event with code "DS_UP- LOAD_REQ" has been received

2. After step c), check that State_Property.DS_UPLOAD_FLAG = "0"

3. After step e), check that no timeout occurred and Event with code "DS_UP-

LOAD_REQ" has been received

4. After step f), check that State_Property.DS_UPLOAD_FLAG = "1"

Test passed All evaluations with positive result

Test not passed (examples) No response or any evaluation failed

Report Single Parameter without DS activity <ok | nok> DS_DownloadStore in ParameterManager state "Idle_0" <ok | nok>

1239

### Test Specification © IO-Link – 159 – Version V1.1.5

### 1240 7.7.12 Clear DS_UPLOAD_FLAG after DS_Up/DownloadEnd

### 1241 Table 108 defines the test conditions for this test case.

### 1242 Table 108 – Clear DS_UPLOAD_FLAG after DS_Up/DownloadEnd

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0322

Name TCD_DS_APP_IDLEFLAGCLEAR

Purpose (short) DS_UPLOAD_FLAG is cleared after TransmissionEnd while in DSIdle_2

Equipment under test (EUT) Device with Data Storage indicated in IODD

Test case version 1.0

Category / type Device DS test: Test to pass

Specification (clause) [1], see 10.4.2, Table B.11, Table D.2, Figure 90

Configuration / setup Device-Tester-Unit 1243

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) DS_UPLOAD_FLAG is cleared when DS_UploadEnd or DS_DownloadEnd command is received while Data Storage state machine is in state DSIdle_2 (Transition T11)

Precondition DTU: OPERATE EUT: DS state machine is in state DSIdle_2

Procedure a) Write SystemCommand “ParamDownloadStore” b) Read DataStorageIndex c) Write DS Command “DS_UploadEnd” d) Read DataStorageIndex e) Write SystemCommand “ParamDownloadStore” f) Read DataStorageIndex g) Write DS Command “DS_DownloadEnd h) Read DataStorageIndex

Test parameter –

Post condition – 1244

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "DS_UPLOAD_FLAG" in step b), d), f), and h)

Test passed In b) = "1" In d) = "0" In f) = "1" In h) = "0"

Test not passed (examples) Any check failed

Report In b): <flag> <ok | nok> In d): <flag> <ok | nok> In f): <flag> <ok | nok> In h): <flag> <ok | nok> 1245

### Version V1.1.5 – 160 – Test Specification © IO-Link

### 1246 7.7.13 Storage of DS_UPLOAD_FLAG in non volatile memory

### 1247 Table 109 defines the test conditions for this test case.

### 1248 Table 109 – Storage of DS_UPLOAD_FLAG in non volatile memory

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0374

Name TCD_DS_APP_UPLOAD_FLAG_NON_VOLATILE

Purpose (short) Storage of DS_UPLOAD_FLAG in non volatile memory

Equipment under test (EUT) Device with Data Storage support

Test case version 1.0

Category / type Device DS test: test to pass

Specification (clause) [7], see 10.4.2, Tables B.8, B.9, B.11, and D.1, Figure 90

Configuration / setup Device-Tester-Unit 1249

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test covers parameterization (parameter set 2) of a Device and initiation of the Upload via SystemCommand "ParamDownloadStore". Device shall store the DS_UP- LOAD_FLAG in non volatile memory

Precondition - Device in PREOPERATE mode

- Parameter set 1 stored within Device (manufacturer to define parameter set 1)

- DS_UPLOAD_FLAG is not set

Procedure a) Call SystemCommand "ParamDownloadStore" (causes Event DS_UPLOAD_REQ)

b) Wait for event DS_UPLOAD_REQ

c) Check DS_UPLOAD_FLAG in parameter DataStorageIndex

d) Apply power cycle (power off, wait 5 s, power on Master restarts communication)

e) Check if a communication startup sequence has been triggered, and Device is in PREOPERATE

f) Check DS_UPLOAD_FLAG in parameter DataStorageIndex

g) Reset the DS_UPLOAD_FLAG with the call of the System Command “DS_Upload- Start”, followed by the system command “DSUploadEnd”

h) Check DS_UPLOAD_FLAG in parameter DataStorageIndex

Test parameter -

Post condition - 1250

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check whether Event DS_UPLOAD_REQ was raised automatically by the Device after b)

2. check via Data Storage Index: the DS_UPLOAD_FLAG is set after c)

3. Startup Sequence has been detected in e), and Device is in PREOPERATE state

4. Value of procedure f) (DS_UPLOAD_FLAG) = "1" (active DS_UPLOAD_REQ)

5. Value of procedure h) (DS_UPLOAD_FLAG) = "0" (no active DS_UPLOAD_REQ)

Test passed All checks are positive

Test failed (examples) Any of the checks failed

Report Device has stored the DS_UPLOAD_FLAG non volatile: <yes/no> <ok | nok>

1251

1252

Test Specification © IO-Link – 161 – Version V1.1.5

1253 7.8 Operation with a legacy Master ("Master 1.0")

1254 7.8.1 General

1255 Designers of Devices according to [1] ("Device 1.1") should be aware of the possibility that such 1256 a Device in the field can be connected to a Master ("Master 1.0") designed according to a 1257 previous specification [5]. Therefore, the conformity class requirements and the associated test 1258 cases are specified within the following clauses. It should be noted that the IODD of such a 1259 Device plays an important role in establishing the right behavior in respect to Process Data 1260 exchange and cycle times (interleave mode). The compatibility rules for IODDs are defined in 1261 [3].

1262 7.8.2 Conformity classes

1263 7.8.2.1 Master conformity

1264 The "Masters 1.0" in the field are supposed to be conform with [5]. By design according to [1][1], 1265 the "Masters 1.1" shall be compatible to any legacy "Device 1.0". Therefore, no special com- 1266 patibility rules are required for Master and no conformity classes.

1267 7.8.2.2 "Device 1.1" without backward compatibility

1268 The Device requires features that only a Master provides, which is designed according to [1] or 1269 a later version. Thus, usually it can deny SDCI communication with a "Master 1.0". Example is 1270 a Device with large Process Data (PD). If this Device would be used with a "Master 1.0" and an 1271 M-sequence TYPE_1, the Process Data cycle could last much longer than with a "Master 1.1" 1272 (see Annex B.5).

1273 7.8.2.3 "Device 1.1" compatible with a "Master 1.0"

1274 There exist two main possibilities to design a "Device 1.1" compatible to a "Master 1.0".

1275 • A "Device 1.1" can be adjusted to a behavior according to [5] through setting of a specific 1276 parameter using ISDU services ("Device 1.0"). In this case no new test cases are required 1277 due to an SDCI communication compatible to [5].

1278 • A "Device 1.1" will automatically detect connection to a "Master 1.0" and switch to an SDCI 1279 communication compatible to [5]. For this case, the restrictions for "Master 1.0" and "Device 1280 1.1" are defined in the following.

1281 The compatibility restrictions or constraints for a "Master 1.0" being able to support automatic 1282 SDCI communication version detection are:

1283 • As defined in the state machines of [1], the "Device 1.1" will send the protocol version 1.1 1284 (or a later one) via parameter 0x04 (RevisionID) to the "Master 1.0" during the startup 1285 phase. The "Master 1.0" ignores this version number. If the "Master 1.0" insists in protocol 1286 version 1.0, the "Device 1.1" cannot be used with this Master.

1287 • During reading of the parameters 0x02 to 0x06 (Direct Parameter page 1) in the 1288 STARTUP phase, the "Device 1.1" cannot detect the Master version. For this reason, 1289 some of the reserved bits in the parameter 0x03 (M-sequence Capability) in [5] are set in 1290 the "Device 1.1". The "Master 1.0" ignores these bits. Otherwise, the "Device 1.1" cannot 1291 be used with this "Master 1.0". 1292

1293

### Version V1.1.5 – 162 – Test Specification © IO-Link

### 1294 7.8.3 From STARTUP to OPERATE (V1.0)

### 1295 Table 110 defines the test conditions for this test case.

### 1296 Table 110 – From STARTUP to OPERATE (V1.0)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0085

Name TCD_DLIC_COMP_STARTUP

Purpose (short) Establish a connection from Wakeup to OPERATE according to V1.0 SDCI protocol

Equipment under test (EUT) Device with "V1.0" support

Test case version 1.2

Category / type Device legacy Master test: test to pass

Specification (clause) [5]

Configuration / setup Device-Tester-Unit 1297

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Establish a connection from Wakeup to OPERATE in V1.0 way of SDCI protocol. The Device-Tester initiates a Wakeup request, reads out the parameter 0x02 – 0x06 (DPP1) and then tries to turn the Device in OPERATE mode. After this, the Device shall be able to exchange Process Data and accept ISDU services. This test shall ensure that a V1.1 Device can be accessed from a V1.0 Master.

Precondition –

Procedure a) Master initiates a Wakeup (with reading of parameter 0x02) b) After the transmission rate detection the Master reads parameter 0x03 – 0x06 c) After reading the parameter it will write the MasterCycleTime d) Master sends MasterCommand 0x99 (OPERATE) e) Master switches to target M-sequence type and exchanges Process Data f) Master initiates a Read or Write to DPP1 to ensure response g) Master initiates an ISDU Read or Write to test the Device capability (only in case of ISDU support)

Test parameter –

Post condition – 1298

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check if Device responds with the correct values during STARTUP

2. Check if the Device responds with the correct M-sequence type after OPERATE

3. Check if the Device supports ISDU (highly recommended)

4. Check if the ISDU Read or Write is responded (only if ISDU is supported)

Test passed Evaluations 1), 2), and 4) successful

Test not passed (examples) Evaluations 1), 2), or 4) failed

Report Exchange of PD: <yes/no> <ok | nok> ISDU is working: <yes/no> <ok | nok> No ISDU support: <yes/no> <exeption>

1299

1300

### Test Specification © IO-Link – 163 – Version V1.1.5

### 1301 7.8.4 From STARTUP to OPERATE – interleave (V1.0)

### 1302 Table 111 defines the test conditions for this test case.

### 1303 Table 111 – From STARTUP to OPERATE - interleave (V1.0)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0086

Name TCD_DLIC_COMP_TYPE1INTERLEAVE

Purpose (short) Interleave test

Equipment under test (EUT) Device with "V1.0" support

Test case version 1.2

Category / type Device legacy Master test: test to pass

Specification (clause) [5]

Configuration / setup Device-Tester-Unit 1304

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Interleave test. In V1.1 the SDCI protocol defines new M-sequence types for large Process Data transfers (more than 2 octets). In Version V1.0, SDCI communication uses the TYPE_1_x M-sequences with interleaving of PD and OD. A compatible Device shall be able to switch to the interleave mode (TYPE_1_x) during communication with the Device-Tester.

Precondition Device supports more than 2 octets of Process Data

Procedure a) Master initiates a Wakeup request (with reading of parameter 0x02) b) After detection of the transmission rate the Master reads parameter 0x03 – 0x06 c) After reading the parameter it will write the MasterCycleTime to 0x01 d) Master sends MasterCommand 0x99 (OPERATE) e) Master switches to M-sequence TYPE_1_1/2 and exchanges Process Data f) Master initiates a Read or Write DPP1 to ensure a response from the Device g) Master initiates an ISDU Read or Write to ensure a response from the Device (only in case of ISDU support)

Test parameter "PD size" taken from the IODD

Post condition – 1305

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check if Device responds with the correct values during STARTUP

2. Check if the Device responds with the correct M-sequence type after OPERATE

3. Check if the Read or Write to DPP1 is responded

4. Check if the ISDU Read or Write is responded

Test passed Evaluations 1), 2), and 4) successful

Test not passed (examples) Evaluations 1), 2), or 4) failed

Report Exchange of PD in interleave mode: <yes/no> <ok | nok> Read or Write to DPP1 is working: <yes/no> <ok | nok> ISDU is working: <yes/no> <ok | nok> No ISDU support: <yes/no> <exeption>

1306

1307

### Version V1.1.5 – 164 – Test Specification © IO-Link

### 1308 7.8.5 Events – PDInvalid / PDValid (V1.0)

### 1309 Table 112 defines the test conditions for this test case.

### 1310 Table 112 – Events – PDInvalid / PDValid (V1.0)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0087

Name TCD_DLIC_COMP_PDINVALIDEVENT

Purpose (short) Send Event for PDVALID/INVALID

Equipment under test (EUT) Device NOTE For Devices without PDInvalid Flag support, this test case can be skipped. No test interface required.

Test case version 1.1

Category / type Device legacy Master test: test to pass

Specification (clause) [5]

Configuration / setup Device-Tester-Unit 1311

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Send Event for PDVALID/INVALID. This test to ensure that a Device can indicate the validity of the Process Data inputs GOOD or BAD via an Event in a V1.0 SDCI communication (In V1.0, the Master does not interpret the bit in the Process Data).

Precondition Communication is established

Procedure a) Stimulate an error in the Device, which leads to a Process Data failure b) The Device shall send this status change via an Event

Test parameter –

Post condition – 1312

TEST CASE RESULTS CHECK / REACTION

Evaluation Check if the Device sent an Event with PDValid/Invalid to the Master

Test passed DTU received an Event for the validity change of Process Data (GOOD and BAD)

Test not passed (examples) Device did not send an Event

Report Device sent "GOOD/BAD" Event: <yes/no> <ok | nok>

1313

1314

### Test Specification © IO-Link – 165 – Version V1.1.5

### 1315 7.9 Direct Parameter page 1

### 1316 7.9.1 MasterCycleTime

### 1317 Table 113 defines the test conditions for this test case.

### 1318 Table 113 – MasterCycleTime

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0089

Name TCD_DLPC_STDP_MASTERCYCLETIME

Purpose (short) Correct value of MasterCycleTime

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device DPP test: test to pass

Specification (clause) [1], see B.1.3

Configuration / setup Device-Tester-Unit 1319

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct value of MasterCycleTime in DPP1. The value shall match the value transmitted by the Master (DTU).

Precondition DTU: Port inactive EUT: –

Procedure a) Set DTU to communication;save MasterCycleTime(Master) b) Read DPP1.MasterCycleTime;returns MasterCycleTime(Device)

Test parameter –

Post condition – 1320

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After b), compare MasterCycleTime(Master) with MasterCycleTime(Device)

Test passed Values match

Test not passed (examples) Mismatch of values

Report MasterCycleTime(Master): <value> MasterCycleTime(Device): <value> <ok | nok>

1321

1322

### Version V1.1.5 – 166 – Test Specification © IO-Link

### 1323 7.9.2 MinCycleTime

### 1324 Table 114 defines the test conditions for this test case.

### 1325 Table 114 – MinCycleTime

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0090

Name TCD_DLPC_STDP_MINCYCLETIME

Purpose (short) Correct setting of MinCycleTime as indicated in IODD

Equipment under test (EUT) Device

Test case version 1.2

Category / type Device DPP test: test to pass

Specification (clause) [1], see B.1.3, Figure B.2, Table B.3

Configuration / setup Device-Tester-Unit 1326

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test value of MinCycleTime. The value shall match the Device specific default settings as indicated in IODD and it shall be valid according to specified coding.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read DPP1.MinCycleTime;returns MinCycleTime(Device)

Test parameter MinCycleTimeIODD = node PhysicalLayer, attribute minCycleTime in IODD

Post condition – 1327

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After a), compare MinCycleTime(Device) with MinCycleTimeIODD

Test passed MinCycleTime(Device) = MinCycleTimeIODD and Time Base < 3

Test not passed (examples) MinCycleTime(Device) ≠ MinCycleTimeIODD or Time Base = 3

Report MinCycleTimeIODD: <value> MinCycleTime(DEVICE): <value> <ok | nok> Time Base: <value> <ok | nok>

1328

1329

### Test Specification © IO-Link – 167 – Version V1.1.5

### 1330 7.9.3 M-sequenceCapability

### 1331 Table 115 defines the test conditions for this test case.

### 1332 Table 115 – M-sequenceCapability

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0091

Name TCD_DLPC_STDP_FSEQCAPABILITY

Purpose (short) Correct M-sequence type entries as indicated in IODD

Equipment under test (EUT) Device

Test case version 1.2

Category / type Device DPP test: test to pass

Specification (clause) [1], see B.1.4, Figure B.3

Configuration / setup Device-Tester-Unit 1333

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test parameter M-sequenceCapability for a valid value according to specified coding. Values shall match the Device specific settings as specified in IODD.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read DPP1.M-sequenceCapability;returns M-sequenceCapability(Device)

Test parameter M-sequenceCapabilityIODD = node PhysicalLayer, attribute mSequenceCapability

Post condition – 1334

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After a), check value of M-sequenceCapability

Test passed M-sequenceCapability(Device) = M-sequenceCapabilityIODD and bit 6,7 = "0"

Test not passed (examples) M-sequenceCapability(Device) ≠ M-sequenceCapabilityIODD or bit 6,7 ≠ "0"

Report M-sequenceCapabilityIODD: <value> M-sequenceCapability(Device): <value> <ok | nok>

1335

1336

### Version V1.1.5 – 168 – Test Specification © IO-Link

### 1337 7.9.4 RevisionID

### 1338 Table 116 defines the test conditions for this test case.

### 1339 Table 116 – RevisionID

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0092

Name TCD_DLPC_STDP_REVISIONID

Purpose (short) Correct default protocol revision as indicated in IODD

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device DPP test: test to pass

Specification (clause) [1], see B.1.5, Figure B.4

Configuration / setup Device-Tester-Unit 1340

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test value of the protocol revision of Device. Value shall match the revision defined in IODD.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read DPP1.RevisionID;returns RevisionID(Device)

Test parameter ProtocolRevisionIODD = node CommNetworkProfile, attribute iolinkRevision

Post condition – 1341

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After a), check RevisionID(Device)

Test passed RevisionID(Device) = ProtocolRevisionIODD

Test not passed (examples) RevisionID(Device) does not match

Report ProtocolRevisionIDIODD: <value> RevisionID(Device): <value> <ok | nok>

1342

1343

### Test Specification © IO-Link – 169 – Version V1.1.5

### 1344 7.9.5 ProcessDataIn

### 1345 Table 117 defines the test conditions for this test case.

### 1346 Table 117 – ProcessDataIn

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0093

Name TCD_DLPC_STDP_PDIN

Purpose (short) Correct default ProcessDataInput value as indicated in IODD

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device DPP test: test to pass

Specification (clause) [1], see B.1.6, Figure B.5, Table B.5, Table B.6

Configuration / setup Device-Tester-Unit 1347

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the values of ProcessDataInput. The value shall match the value as specified in the IODD.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read DPP1.ProcessDataIn;returns ProcessDataIn(Device)

Test parameter ProcessDataInIODD = node ProcessData.ProcessDataIn, attribute bitlength SIOsupportIODD = node PhysicalLayer, attribute sioSupported

Post condition – 1348

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After a), check ProcessDataIn(Device)

Test passed Process Data length and SIO bit match specified values, and Process Data length unit is a valid value, and Bit 5 is "0", and ProcessDataIn(Device) = ProcessDataInIODD

Test not passed (examples) Any of the evaluations failed

Report ProcessDataInIODD: <value> SIOsupportIODD: <value> ProcessDataIn(Device): <value> <ok | nok>

1349

1350

### Version V1.1.5 – 170 – Test Specification © IO-Link

### 1351 7.9.6 ProcessDataOut

### 1352 Table 118 defines the test conditions for this test case.

### 1353 Table 118 – ProcessDataOut

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0094

Name TCD_DLPC_STDP_PDOUT

Purpose (short) Correct default ProcessDataOutput value as indicated in IODD

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device DPP test: test to pass

Specification (clause) [1], see B.1.7, Figure B.5, Table B.6

Configuration / setup Device-Tester-Unit 1354

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the values of Process Data Output. The value shall match the value as specified in the IODD.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read DPP1.ProcessDataOut;returns ProcessDataOut(Device)

Test parameter ProcessDataOutIODD = node ProcessData.ProcessDataOut, attribute bitLength

Post condition – 1355

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After a), check ProcessDataOut(Device)

Test passed Process Data length match specified values, and Process Data length unit is a valid value, and Bit 5 and 6 are "0", and ProcessDataOut(Device) = ProcessDataOutIODD

Test not passed (examples) Any of the evaluations failed

Report ProcessDataOutIODD: <value> ProcessDataOut(Device): <value> <ok | nok>

1356

1357

### Test Specification © IO-Link – 171 – Version V1.1.5

### 1358 7.9.7 VendorID

### 1359 Table 119 defines the test conditions for this test case.

### 1360 Table 119 – VendorID

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0095

Name TCD_DLPC_STDP_VENDORID

Purpose (short) Correct VendorID as indicated in IODD

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device DPP test: test to pass

Specification (clause) [1], see B.1.8

Configuration / setup Device-Tester-Unit 1361

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the value of the VendorID. The value shall match the unique ID assigned to the vendor and the value specified in the IODD.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read DPP1.VendorID1;returns VendorID1 b) Read DPP1.VendorID2;returns VendorID2 c) Combine VendorID1 and VendorID2 to VendorID(Device)

Test parameter VendorIDIODD = node DeviceIdentity, attribute vendorId VendorIDAssigned = VendorID @ (https://io-link.com/share/Downloads/Vendor_ID_Table.xml)

Post condition – 1362

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After c), check VendorID(Device)

Test passed VendorID(Device) = VendorIDIODD, and VendorID(Device) = VendorIDAssigned

Test not passed (examples) Any of the evaluations failed

Report VendorIDIODD: <value> VendorIDAssigned: <value> VendorID(Device): <value> <ok | nok>

1363

1364

### Version V1.1.5 – 172 – Test Specification © IO-Link

### 1365 7.9.8 DeviceID

### 1366 Table 120 defines the test conditions for this test case.

### 1367 Table 120 – DeviceID

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0096

Name TCD_DLPC_STDP_DEVICEID

Purpose (short) Correct default DeviceID as indicated in IODD

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device DPP test: test to pass

Specification (clause) [1], see B.1.9

Configuration / setup Device-Tester-Unit 1368

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the value of the DeviceID. The value shall match the ID assigned by the vendor and the value specified in the IODD.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read DPP1.DeviceID1;returns DeviceID1 b) Read DPP1.DeviceID2;returns DeviceID2 c) Read DPP1.DeviceID3;returns DeviceID3 d) Combine DeviceID1, and DeviceID2, and DeviceID3 to DeviceID(Device)

Test parameter DeviceIDIODD = node DeviceIdentity, attribute deviceId

Post condition – 1369

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After d), check DeviceID(Device)

Test passed DeviceID(Device) = DeviceIDIODD, and DeviceID(Device) > 0

Test not passed (examples) Any of the evaluations failed

Report DeviceIDIODD: <value> DeviceID(Device): <value> <ok | nok>

1370

1371

### Test Specification © IO-Link – 173 – Version V1.1.5

### 1372 7.9.9 FunctionID

### 1373 Table 121 defines the test conditions for this test case.

### 1374 Table 121 – FunctionID

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0097

Name TCD_DLPC_STDP_FUNCTIONID

Purpose (short) Correct FunctionID (reserved)

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device DPP test: test to pass

Specification (clause) [1], see B.1.10

Configuration / setup Device-Tester-Unit 1375

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the value of the FunctionID. The FunctionID is not used and shall contain the default value.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read DPP1.FunctionID1;returns FunctionID1 b) Read DPP1.FunctionID2;returns FunctionID2 c) Combine FunctionID1 and FunctionID2 to FunctionID(Device)

Test parameter –

Post condition – 1376

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After c), check FunctionID(Device)

Test passed FunctionID(Device) = "0"

Test not passed (examples) FunctionID(Device) ≠ "0"

Report FunctionID(Device): <value> <ok | nok>

1377

1378

### Version V1.1.5 – 174 – Test Specification © IO-Link

### 1379 7.9.10 Reserved parameter – Read/Write

### 1380 Table 122 defines the test conditions for this test case.

### 1381 Table 122 – Reserved parameter – Read/Write

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0101

Name TCD_DLPC_STDP_WRITERESPAR

Purpose (short) Test reserved Direct Parameter write behavior

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device DPP test: test to pass

Specification (clause) [1], see B.1.1

Configuration / setup Device-Tester-Unit 1382

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the Device behavior and check result after write access to a reserved Direct Parameter

Precondition Device is in SDCI communication mode

Procedure Write values 0x00 to 0xFF via the Device-Tester to reserved parameters on Direct Parameter page 1 (Address 0x0E)

Input parameter -

Post condition - 1383

TEST CASE RESULTS CHECK / REACTION

Evaluation Check validity of a Write access to reserved Direct Parameter.

Test passed If no communication errors occurred

Test failed (examples) If communication errors or Events occurred

Report Communication errors: <no/yes> <passed/failed> 1384

### Test Specification © IO-Link – 175 – Version V1.1.5

### 1385 7.9.11 Validity of MinCycleTime

### 1386 Table 123 defines the test conditions for this test case.

### 1387 Table 123 – Validity of MinCyleTime

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0375

Name TCD_DLPC_STDP_MINCYCLETIME_VALID

Purpose (short) Check validity of MinCycleTime against best case timing and allowed ranges

Equipment under test (EUT) Device with MinCycleTime <> zero

Test case version 1.0

Category / type Device DPP test, test to pass

Specification (clause) B.1.3

Configuration / setup Device-Tester-Unit 1388

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test value of MinCycleTime against best case timing, means the provided time shall not be lower than the shortest possible timing calculated according to clauses A.3.4 to A.3.6 with t1, t2, tidle equal zero TBIT and tA equal one TBIT. Further the ranges of Table B.3 are checked for violations.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read DPP1.MinCycleTime b) Read DPP1.M-sequenceCapability c) Read DPP1.ProcessDataIn d) Read DPP1.ProcessDataOut

Input parameter -

Post condition - 1389

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) After reading the communication parameters, calculate best timing with t 1, t2, tidle equal zero and tA equal one based on the proposed M-sequence type and process data width.

2. Check MinCycleTime against allowed ranges defined in Table B.3

Test passed MinCycleTime equal or greater than best timing from 1) MinCycleTime does not violate allowed ranges according 2)

Test failed (examples) Min CycleTime lower than best timing or violating ranges according 2)

Report MinCycleTime in range <ok | nok>

1390

Version V1.1.5 – 176 – Test Specification © IO-Link

1391 7.10 Predefined Device parameters

1392 7.10.1 General rules

1393 Predefined parameters shall be tested in any case. The following rules apply:

1394 a) They shall be tested as specified within the test cases if they are defined within the IODD.

1395 b) They shall not be tested as specified within the test cases if they are not defined within the 1396 IODD.

1397 c) All optional test cases for Predefined Parameters shall be handled according to rule b) 1398 1399

### Test Specification © IO-Link – 177 – Version V1.1.5

### 1400 7.10.2 System command – reserved commands

### 1401 Table 124 defines the test conditions for this test case.

### 1402 Table 124 – System command – reserved commands

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0104

Name TCD_DLIC_DEFP_SYSCMDRES

Purpose (short) SystemCommand behavior upon reserved commands (via ISDU)

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see B.2.2, and Annex C.2.1, Table C.1

Configuration / setup Device-Tester-Unit 1403

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The test verifies the correct response values upon usage of reserved and unused SystemCommands.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write subsequently the following values to parameter SystemCommand:

- 0x00

- 0x01 to 0x04, if Block Parameter transfer is not supported

- 0x05, if neither Data Storage nor Block Parameter transfer is supported

- 0x06, if Block Parameter transfer is not supported

- 0x07 to 0x3F

- 0x40 to 0x7F, if no Profile is supported

- 0x80 to 0x83, if not referenced in IODD

- 0x84 to 0x9F

- 0xA0 to 0xFF, if not referenced in IODD

Test parameter –

Post condition – 1404

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check each Write response in a)

Test passed All Write error responses = FUNC_NOTAVAIL (0x8035) If Write error response ≠ FUNC_NOTAVAIL (0x8035), indicate value and warning

Test not passed (examples) Positive or no Write response

Report Write response (reserved SystemCommand): <value> <ok | nok>

1405

### Version V1.1.5 – 178 – Test Specification © IO-Link

### 1406 7.10.3 Data Storage Index – entire parameter structure

### 1407 Table 125 defines the test conditions for this test case.

### 1408 Table 125 – Data Storage Index – entire parameter structure

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0107

Name TCD_DLIC_DEFP_DSINDEX

Purpose (short) Behavior of parameter DataStorageIndex

Equipment under test (EUT) Device

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.3, Table B.9; see B.2.3 (records without gaps)

Configuration / setup Device-Tester-Unit 1409

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The test verifies correct implementation of parameter DataStorageIndex. Devices without Data Storage support shall support the entire parameter structure.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter DataStorageIndex (Index 0x03)

Test parameter –

Post condition – 1410

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response of Read access

2. Check parameter length

3. Calculate <index entries> = (parameter length – 12)/3

Test passed No negative response, and <index entries> has a positive integer value in the range ≥ 0 and ≤ 70

Test not passed (examples) Negative response, or <index entries> outside the permitted range

Report Data Storage Index, length: <index entries> <ok | nok>

1411

1412

### Test Specification © IO-Link – 179 – Version V1.1.5

### 1413 7.10.4 Data Storage Index – record items

### 1414 Table 126 defines the test conditions for this test case.

### 1415 Table 126 – DataStorageIndex – record items

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0108

Name TCD_DLIC_DEFP_DSRECORD

Purpose (short) Behavior of parameter DataStorageIndex record items

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.3, Table B.9

Configuration / setup Device-Tester-Unit 1416

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The test verifies the correct structure of parameter and record items of the DataStorageIndex.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read subsequently Subindex 1 to 5 of DataStorageIndex (Index 0x03)

Test parameter –

Post condition – 1417

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response of Read access

2. Check record item length

3. Check value of record item

Test passed No negative response on record item Read access, and Subindex 1 has a length of 1 octet and value is within range ≥ 0 to ≤ 5 Subindex 2 has a length of 1 octet and value of bit 0 and bit 3 - 6 is "0" Subindex 3 has a length of 4 octets and value is within range 0 to 2048 Subindex 4 has a length of 4 octets Subindex 5 has a length of ≥ 2 octets in increments of 3 (2,5,8,11,14, until 212)

Test not passed (examples) Record items are deviating in length or value range

Report DataStorageIndex, Subindex 1: <length, value> <ok | nok> DataStorageIndex, Subindex 2: <length, value> <ok | nok> DataStorageIndex, Subindex 3: <length, value> <ok | nok> DataStorageIndex, Subindex 4: <length> <ok | nok> DataStorageIndex, Subindex 5: <length> <ok | nok>

1418

### Version V1.1.5 – 180 – Test Specification © IO-Link

### 1419 7.10.5 Data Storage Index – termination marker

### 1420 Table 126 defines the test conditions for this test case.

### 1421 Table 127 – DataStorageIndex – termination marker

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0323

Name TCD_DLIC_DEFP_DSRECORDMARKER

Purpose (short) Behavior of parameter DataStorageIndex record items – Termination marker

Equipment under test (EUT) Device without DS support

Test case version 1.0

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.3, Table B.9, and 10.4.5

Configuration / setup Device-Tester-Unit 1422

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The test verifies the correct entries in items Data_Storage_Size and DS Index_List of the parameter DataStorageIndex. Both items shall be "0".

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read subsequently Subindex 1 to 5 of DataStorageIndex (Index 0x03)

Test parameter –

Post condition – 1423

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response of Read access

2. Check record item length

3. Check value of record item

Test passed No negative response on record item Read access, and Subindex 1 has a length of 1 octet and value is within range ≥ 0 to ≤ 5 Subindex 2 has a length of 1 octet and value of bit 0 and bit 3 - 6 is "0" Subindex 3 has a length of 4 octets and value = "0" Subindex 4 has a length of 4 octets Subindex 5 has a length of 2 octets and value = "0"

Test not passed (examples) Record items are deviating in length or value range

Report DataStorageIndex, Subindex 1: <length, value> <ok | nok> DataStorageIndex, Subindex 2: <length, value> <ok | nok> DataStorageIndex, Subindex 3: <length, value> <ok | nok> DataStorageIndex, Subindex 4: <length> <ok | nok> DataStorageIndex, Subindex 5: <length, value > <ok | nok>

1424

1425

### Test Specification © IO-Link – 181 – Version V1.1.5

### 1426 7.10.6 Device Access Locks – valid

### 1427 Table 128 defines the test conditions for this test case.

### 1428 Table 128 – Device Access Locks – valid

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0109

Name TCD_DLIC_DEFP_ACCESSLOCKSVAL

Purpose (short) Behavior of DeviceAccessLocks with valid values

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], 10.6.5, 10.6.6, Annex B.2.4, Table B.12

Configuration / setup Device-Tester-Unit 1429

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The test verifies that all implemented values for DeviceAccessLocks are stored and responded correctly.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write value to DeviceAccessLocks with "1" at every bit position marked as implemented and "0" at the remaining bits. b) Read value DeviceAccessLocks c) Write value 0x0000 to DeviceAccessLocks d) Read value DeviceAccessLocks

Test parameter –

Post condition – 1430

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check for response on Read and Write access

2. Set warning flag, if a positive response is received for a write access with “1”

at bit positions "0" or "1"

3. Compare response value from step b) with written value from step a)

4. Compare response value from step d) with written value from step c)

Test passed No negative response on Read or Write access, and Evaluations 3) and 4) show matching values

Test not passed (examples) No matching values

Report DeviceAccessLocks implemented: <written/read values> <ok | nok> DeviceAccessLocks 0x0000: <written/read values> <ok | nok> Warning "Locking implemented" (see NOTE): <exception>

NOTE If warning flag is set, text shall be displayed: "It is highly recommended not to implement the features "parameter access locking" or "Data Storage locking" since they will be removed in future releases of the specification. Vendor to inform user".

1431

1432

### Version V1.1.5 – 182 – Test Specification © IO-Link

### 1433 7.10.7 Device Access Locks – invalid

### 1434 Table 129 defines the test conditions for this test case.

### 1435 Table 129 – Device Access Locks – invalid

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0110

Name TCD_DLIC_DEFP_ACCESSLOCKSINVAL

Purpose (short) Behavior of DeviceAccessLocks with invalid values

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.0

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.4, Table B.11

Configuration / setup Device-Tester-Unit 1436

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The test verifies that all reserved or unused values for DeviceAccessLocks are responded correctly.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write to DeviceAccessLocks subsequently the value "1" at a single bit position, which is marked as reserved and a value "0" at the remaining bit positions b) Read value DeviceAccessLocks c) Repeat from a) with next bit position

Test parameter –

Post condition – 1437

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check responses in step a)

2. Check response values in step b)

Test passed Each response in 1) = PAR_VALOUTOFRNG (0x8030), and Each value = 0x0000

Test not passed (examples) No negative response Any value ≠ 0x0000

Report Device Access Locks reserved: <written/read values> <ok | nok> Device Access Locks 0x0000: <written/read values> <ok | nok>

1438

1439

### Test Specification © IO-Link – 183 – Version V1.1.5

### 1440 7.10.8 Profile Characteristic

### 1441 Table 130 defines the test conditions for this test case.

### 1442 Table 130 – Profile Characteristic

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0111

Name TCD_DLIC_DEFP_PROFILCHARAC

Purpose (short) Behavior of parameter ProfileCharacteristic

Equipment under test (EUT) Device without supported profiles

Test case version 1.2

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.5; [3]

Configuration / setup Device-Tester-Unit 1443

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test for implementation of parameter ProfileCharacteristic

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter ProfileCharacteristic (Index 0x000D)

Test parameter –

Post condition – 1444

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

Test passed Negative response with ErrorType = 0x8011 ─ Index not available

Test not passed (examples) Different ErrorType or positive response

Report Response: <negative/positive> <ok | nok> ErrorType: <value> <ok | nok>

1445

1446

### Version V1.1.5 – 184 – Test Specification © IO-Link

### 1447 7.10.9 PD Input Descriptor

### 1448 Table 131 defines the test conditions for this test case.

### 1449 Table 131 – PD Input Descriptor

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0112

Name TCD_DLIC_DEFP_PDINDESC

Purpose (short) Behavior of parameter PDInputDescriptor

Equipment under test (EUT) Device without supported profiles

Test case version 1.3

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.6; [3]

Configuration / setup Device-Tester-Unit 1450

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test for implementation of parameter PDInputDescriptor

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter PDInputDescriptor (Index 0x000E)

Test parameter –

Post condition – 1451

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

Test passed Negative response with ErrorType 0x8011 ─ Index not available

Test not passed (examples) Different ErrorType or positive response

Report Response: <negative/positive> <ok | nok> ErrorType: <value> <ok | nok>

1452

1453

### Test Specification © IO-Link – 185 – Version V1.1.5

### 1454 7.10.10 PD Output Descriptor

### 1455 Table 132 defines the test conditions for this test case.

### 1456 Table 132 – PD Output Descriptor

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0113

Name TCD_DLIC_DEFP_PDOUTDESC

Purpose (short) Behavior of parameter PDOutputDescriptor

Equipment under test (EUT) Device without supported profiles

Test case version 1.2

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.7; [3]

Configuration / setup Device-Tester-Unit 1457

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test for implementation of parameter PDOutputDescriptor

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter PDOutputDescriptor (Index 0x000F)

Test parameter –

Post condition – 1458

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

Test passed Negative response with ErrorType 0x8011 ─ Index not available

Test not passed (examples) Different ErrorType or positive response

Report Response: <negative/positive> <ok | nok> ErrorType: <value> <ok | nok>

1459

1460

### Version V1.1.5 – 186 – Test Specification © IO-Link

### 1461 7.10.11 Vendor Name

### 1462 Table 133 defines the test conditions for this test case.

### 1463 Table 133 – Vendor Name

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0114

Name TCD_DLIC_DEFP_VENDORNAM

Purpose (short) Behavior of parameter VendorName

Equipment under test (EUT) Device with ISDU support

Test case version 1.2

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.6

Configuration / setup Device-Tester-Unit 1464

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter VendorName

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter VendorName (Index 0x0010)

Test parameter –

Post condition – 1465

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

2. Check coding of parameter

3. Check parameter length

4. Check contents of parameter

Test passed No negative response, and Parameter coded in UTF8, and Parameter length ≤ 64 octets, and Contents matches registered vendor for VendorID (VID)

Test not passed (examples) Any of the evaluations fails

Report VendorName response: <negative/positive> <ok | nok> VendorName UTF8 coding: <yes/no> <ok | nok> VendorName length: <value> <ok | nok> VendorName registration: <yes/no> <ok | nok>

1466

1467

### Test Specification © IO-Link – 187 – Version V1.1.5

### 1468 7.10.12 Vendor Text

### 1469 Table 134 defines the test conditions for this test case.

### 1470 Table 134 – Vendor Text

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0115

Name TCD_DLIC_DEFP_VENDORTEXT

Purpose (short) Behavior of parameter VendorText

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.7

Configuration / setup Device-Tester-Unit 1471

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter VendorText

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter VendorText (Index 0x0011)

Test parameter –

Post condition – 1472

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response on Read access

2. Check coding of parameter

3. Check parameter length

4. Check contents of parameter if default value is present in IODD.

Test passed No negative response, and Parameter coded in UTF8, and Parameter length ≤ 64 octets, and Contents matches vendor / Device specific information

Test not passed (examples) Any of the evaluations fails

Report VendorText response: <negative/positive> <ok | nok> VendorText UTF8 coding: <yes/no> <ok | nok> VendorText length: <value> <ok | nok> VendorText adequate: <yes/no> <ok | nok>

1473

1474

### Version V1.1.5 – 188 – Test Specification © IO-Link

### 1475 7.10.13 Product Name

### 1476 Table 135 defines the test conditions for this test case.

### 1477 Table 135 – Product Name

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0116

Name TCD_DLIC_DEFP_PRODUCTNAM

Purpose (short) Behavior of parameter ProductName

Equipment under test (EUT) Device with ISDU support

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.8

Configuration / setup Device-Tester-Unit 1478

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter ProductName

Precondition DTU: Communication EUT: OPERATE

Procedure Read parameter ProductName (Index 0x0012)

Test parameter –

Post condition – 1479

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

2. Check coding of parameter

3. Check parameter length

4. Check contents of parameter if default value is present in IODD.

Test passed No negative response, and Parameter coded in UTF8, and Parameter length ≤ 64 octets, and Contents matches vendor / Device specific information

Test not passed (examples) Any of the evaluations fails

Report ProductName response: <negative/positive> <ok | nok> ProductName UTF8 coding: <yes/no> <ok | nok> ProductName length: <value> <ok | nok> ProductName adequate: <yes/no> <ok | nok>

1480

1481

### Test Specification © IO-Link – 189 – Version V1.1.5

### 1482 7.10.14 Product ID

### 1483 Table 136 defines the test conditions for this test case.

### 1484 Table 136 – Product ID

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0117

Name TCD_DLIC_DEFP_PRODUCTID

Purpose (short) Behavior of parameter ProductID

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.9

Configuration / setup Device-Tester-Unit 1485

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter ProductID.

Precondition DTU: Communication EUT: OPERATE

Procedure Read parameter Product ID (Index 0x0013)

Test parameter –

Post condition – 1486

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

2. Check coding of parameter

3. Check parameter length

4. Check contents of parameter if default value is present in IODD.

Test passed No negative response, and Parameter coded in UTF8, and Parameter length ≤ 64 octets, and Contents matches vendor / Device specific information

Test not passed (examples) Any of the evaluations fails

Report ProductID response: <negative/positive> <ok | nok> ProductID UTF8 coding: <yes/no> <ok | nok> ProductID length: <value> <ok | nok> ProductID adequate: <yes/no> <ok | nok>

1487

1488

### Version V1.1.5 – 190 – Test Specification © IO-Link

### 1489 7.10.15 Product Text

### 1490 Table 137 defines the test conditions for this test case.

### 1491 Table 137 – Product Text

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0118

Name TCD_DLIC_DEFP_PRODUCTTEXT

Purpose (short) Behavior of parameter ProductText

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.10

Configuration / setup Device-Tester-Unit 1492

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter ProductText

Precondition DTU: Communication EUT: OPERATE

Procedure Read parameter Product Text (Index 0x0014)

Test parameter –

Post condition – 1493

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

2. Check coding of parameter

3. Check parameter length

4. Check contents of parameter if default value is present in IODD.

Test passed No negative response, and Parameter coded in UTF8, and Parameter length ≤ 64 octets, and Contents matches vendor / Device specific information

Test not passed (examples) Any of the evaluations fails

Report ProductText response: <negative/positive> <ok | nok> ProductText UTF8 coding: <yes/no> <ok | nok> ProductText length: <value> <ok | nok> ProductText adequate: <yes/no> <ok | nok>

1494

1495

### Test Specification © IO-Link – 191 – Version V1.1.5

### 1496 7.10.16 Serial Number

### 1497 Table 138 defines the test conditions for this test case.

### 1498 Table 138 – SerialNumber

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0119

Name TCD_DLIC_DEFP_SERNUM

Purpose (short) Behavior of parameter SerialNumber

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.2

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.11

Configuration / setup Device-Tester-Unit 1499

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter SerialNumber

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter SerialNumber (Index 0x0015)

Test parameter SerialNumber of the EUT provided by the manufacturer

Post condition – 1500

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

2. Check coding of parameter

3. Check parameter length

Test passed No negative response, and Parameter coded in UTF8, and Parameter length ≤ 16 octets

Test not passed (examples) Any of the evaluation steps fails

Report SerialNumber response: <negative/positive> <ok | nok> SerialNumber UTF8 coding: <yes/no> <ok | nok> SerialNumber length: <value> <ok | nok> SerialNumber adequate: <yes/no> <ok | nok>

1501

1502

### Version V1.1.5 – 192 – Test Specification © IO-Link

### 1503 7.10.17 Hardware Revision

### 1504 Table 139 defines the test conditions for this test case.

### 1505 Table 139 – HardwareRevision

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0120

Name TCD_DLIC_DEFP_HARDREV

Purpose (short) Test of parameter HardwareRevision

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.2

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.12

Configuration / setup Device-Tester-Unit 1506

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter HardwareRevision

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter HardwareRevision (Index 0x0016)

Test parameter HardwareRevision of the EUT provided by the manufacturer

Post condition – 1507

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

2. Check coding of parameter

3. Check parameter length

Test passed No negative response, and Parameter coded in UTF8, and Parameter length ≤ 64 octets

Test not passed (examples) Any of the evaluations fails

Report HardwareRevision response: <negative/positive> <ok | nok> HardwareRevision UTF8 coding: <yes/no> <ok | nok> HardwareRevision length: <value> <ok | nok> HardwareRevision adequate: <yes/no> <ok | nok>

1508

1509

### Test Specification © IO-Link – 193 – Version V1.1.5

### 1510 7.10.18 Firmware Revision

### 1511 Table 140 defines the test conditions for this test case.

### 1512 Table 140 – Firmware Revision

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0121

Name TCD_DLIC_DEFP_FIRMREV

Purpose (short) Behavior of parameter Firmware Revision

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.2

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.13

Configuration / setup Device-Tester-Unit 1513

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter FirmwareRevision

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter FirmwareRevision (Index 0x0017)

Test parameter FirmwareRevision of the EUT provided by the manufacturer

Post condition – 1514

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

2. Check coding of parameter

3. Check parameter length

Test passed No negative response, and Parameter coded in UTF8, and Parameter length ≤ 64 octets

Test not passed (examples) Any of the evaluations fails

Report FirmwareRevision response: <negative/positive> <ok | nok> FirmwareRevision UTF8 coding: <yes/no> <ok | nok> FirmwareRevision length: <value> <ok | nok> FirmwareRevision adequate: <yes/no> <ok | nok>

1515

1516

### Version V1.1.5 – 194 – Test Specification © IO-Link

### 1517 7.10.19 Application Specific Tag – valid

### 1518 Table 141 defines the test conditions for this test case.

### 1519 Table 141 – Application Specific Tag – valid

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0122

Name TCD_DLIC_DEFP_TAGVALID

Purpose (short) Behavior of parameter ApplicationSpecificTag – valid strings

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.2

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.16, 10.3.4, and 10.3.5, Table 98, rule 6

Configuration / setup Device-Tester-Unit 1520

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct behavior for write and read access to parameter ApplicationSpecific- Tag

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write a random text string with length <specified fixed length> to parameter ApplicationSpecificTag (Index 0x0018) b) Read parameter ApplicationSpecificTag (Index 0x0018) c) Power OFF/ON of the Device d) Read parameter ApplicationSpecificTag (Index 0x0018)

Test parameter Random text string with <specified fixed length> = manufacturer dependent (minimum 16 octets, maximum 32 octets).

Post condition – 1521

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response of Read and Write access

2. Compare contents and length of reading in step b) to written string in step a)

3. Compare contents and length of reading in step b) and step d)

Test passed No negative responses, and String lengths in 2) = string lengths in 3) Contents in 2) = Contents in 3)

Test not passed (examples) Negative response or comparison fails

Report ApplicationSpecificTag string length: <ok | nok> ApplicationSpecificTag content: <ok | nok>

1522

1523

### Test Specification © IO-Link – 195 – Version V1.1.5

### 1524 7.10.20 Application Specific Tag – invalid

### 1525 Table 142 defines the test conditions for this test case.

### 1526 Table 142 – Application Specific Tag – invalid

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0123

Name TCD_DLIC_DEFP_TAGINVALID

Purpose (short) Behavior of parameter ApplicationSpecificTag – invalid string length

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.2

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.14

Configuration / setup Device-Tester-Unit 1527

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct behavior for Write and Read access with invalid string length to parameter ApplicationSpecificTag.

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter ApplicationSpecificTag (Index 0x0018) b) Write a random string with length <specified fixed length + 1> to parameter Application Specific Tag (Index 0x0018) c) Read parameter ApplicationSpecificTag (Index 0x0018)

Test parameter Random text string with <specified fixed length> = manufacturer dependent (minimum 16 octets, maximum 32 octets)

Post condition – 1528

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read responses

2. Check Write response

3. Compare contents and length of reading in step a) and step c)

Test passed No negative Read responses, and Negative Write response = VAL_LENOVRRUN (0x8033), and Values in 3) are matching

Test not passed (examples) No response

Report ApplicationSpecificTag negative Write response: <yes/no> <ok | nok> ApplicationSpecificTag contents match: <yes/no> <ok | nok>

1529

1530

### Version V1.1.5 – 196 – Test Specification © IO-Link

### 1531 7.10.21 Error Count

### 1532 Table 143 defines the test conditions for this test case.

### 1533 Table 143 – Error Count

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0124

Name TCD_DLIC_DEFP_ERRCOUNT

Purpose (short) Behavior of parameter ErrorCount

Equipment under test (EUT) Device with adequate reference in IODD (stimulation of ErrorCount possible)

Test case version 1.3

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.19

Configuration / setup Device-Tester-Unit; user interaction for stimulation is required 1534

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter ErrorCount

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter ErrorCount (Index 0x0020) b) Stimulate error within Device specific technology (registered for ErrorCount) c) Read parameter ErrorCount (Index 0x0020) d) Power OFF/ON of the Device;reset of ErrorCount e) Read parameter ErrorCount (Index 0x0020)

Test parameter Manufacturer defined stimulation of an error

Post condition – 1535

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read responses

2. Check parameter length

3. Compare values from step a) and step c) if stimulation is possible

4. Check value of step e)

Test passed No negative Read responses, and Parameter length = 2 octets, and ErrorCount in c) = ErrorCount in a) + 1, and ErrorCount in e) = 0x0000

Test not passed (examples) Any of the evaluations fails

Report ErrorCount: <length> <ok | nok> ErrorCount: <increment> <ok | nok> ErrorCount: <value> <ok | nok> No stimulation possible: <Exception>

1536

1537

### Test Specification © IO-Link – 197 – Version V1.1.5

### 1538 7.10.22 Device Status

### 1539 Table 144 defines the test conditions for this test case.

### 1540 Table 144 – DeviceStatus

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0128

Name TCD_DLIC_DEFP_DEVSTAT

Purpose (short) Behavior of parameter DeviceStatus

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.2

Category / type Device application test: test to pass

### Specification (clause) [1], see Annex B.2.20

Configuration / setup Device-Tester-Unit 1541

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter DeviceStatus

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter DeviceStatus (Index 0x0024)

Test parameter –

Post condition – 1542

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

2. Check parameter length

3. Check parameter value

Test passed No negative response, and Parameter length = 1 octet, and "0" ≤ value ≤ 4

Test not passed (examples) No response

Report DeviceStatus response: <positive/negative> <ok | nok> DeviceStatus length: <value> <ok | nok> DeviceStatus: <value> <ok | nok>

1543

1544

### Version V1.1.5 – 198 – Test Specification © IO-Link

### 1545 7.10.23 Detailed Device Status – complete object

### 1546 Table 145 defines the test conditions for this test case.

### 1547 Table 146 – Detailed Device Status – complete object

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0129

Name TCD_DLIC_DEFP_DETAILDEVSTAT

Purpose (short) Behavior of entire parameter DetailedDeviceStatus

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.2

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.17 and [4]

Configuration / setup Device-Tester-Unit 1548

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test of the correct length of parameter DetailedDeviceStatus

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter DetailedDeviceStatus (Index 0x0025)

Test parameter <record item count>

Post condition – 1549

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check response

2. Check parameter length

3. Calculate <record item count> = parameter length / 3

Test passed No negative response, and Parameter length ≤ 64 x 3 octets, and 1 ≤ record item count is an integer value ≤ 64

Test not passed (examples) No response

Report DetailedDeviceStatus response: <positive/negative> <ok | nok> DetailedDeviceStatus length: <value> <ok | nok> DetailedDeviceStatus: <value> <ok | nok>

1550

1551

### Test Specification © IO-Link – 199 – Version V1.1.5

### 1552 7.10.24 Detailed Device Status – Event active

### 1553 Table 147 defines the test conditions for this test case.

### 1554 Table 147 – Detailed Device Status – Event active

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0131

Name TCD_DLIC_DEFP_DETAILDEVSTATACTIVE

Purpose (short) Record contents in parameter DetailedDeviceStatus with active Event

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.2

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.21

Configuration / setup Device-Tester-Unit 1555

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test of correct entry of active Events in parameter DetailedDeviceStatus

Precondition DTU: Communication EUT: OPERATE, no active Event

Procedure a) Stimulate an incident in Device's specific technology causing an Event , using config7 (error or warning) b) Read record of parameter DetailedDeviceStatus c) Parse result from beginning to end in steps of 3 octets and store value d) Power OFF/ON of the Device e) Read record of parameter DetailedDeviceStatus f) Parse result from beginning to end in steps of 3 octets and store value

Test parameter –

Post condition – 1556

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check value at each procedure step c)

2. Check value at each procedure step f)

Test passed Evaluation 1) One of the parsed Events equals the initiated Event of Step a) Evaluation 2) Check parsed values of Detailed Device Status against the active Events

Test not passed (examples) – Incorrect values or no response

– an initiated event does not appear in the Detailed Device Status

Report DetailedDeviceStatus Event active: <ok | nok>

1557

1558

### Version V1.1.5 – 200 – Test Specification © IO-Link

### 1559 7.10.25 Process Data Input

### 1560 Table 148 defines the test conditions for this test case.

### 1561 Table 148 – Process Data Input

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0132

Name TCD_DLIC_DEFP_PDIN

Purpose (short) Behavior of parameter ProcessDataInput

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.18

Configuration / setup Device-Tester-Unit 1562

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test the correct contents and coding of parameter ProcessDataInput

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read parameter ProcessDataInput (Index 0x0028)

Test parameter –

Post condition – 1563

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read response

2. Check parameter length

3. Check parameter contents

Test passed No negative response, and Parameter length matches parameter ProcessDataIn (DPP1, address 0x05), and Unused bits = "0"

Test not passed (examples) No response

Report ProcessDataInput response: <positive/negative> <ok | nok> ProcessDataInput length: <value> <ok | nok> ProcessDataInput unused bits = "0": <yes/no> <ok | nok>

1564

1565

### Test Specification © IO-Link – 201 – Version V1.1.5

### 1566 7.10.26 Process Data Output

### 1567 Table 149 defines the test conditions for this test case.

### 1568 Table 149 – Process Data Output

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0133

Name TCD_DLIC_DEFP_PDOUT

Purpose (short) Behavior of parameter ProcessDataOutput

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.19

Configuration / setup Device-Tester-Unit 1569

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test of the correct contents and coding of parameter ProcessDataOutput

Precondition DTU: Communication EUT: OPERATE

Procedure Read parameter ProcessDataOutput (Index 0x0029)

Test parameter –

Post condition – 1570

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Read response

2. Check parameter length

3. Check parameter contents

Test passed No negative response, and Parameter length matches parameter ProcessDataOut (DPP1, address 0x06), and Unused bits = "0"

Test not passed (examples) No response

Report ProcessDataOutput response: <positive/negative> <ok | nok> ProcessDataOutput length: <value> <ok | nok> ProcessDataOutput unused bits = "0": <yes/no> <ok | nok>

1571

1572

### Version V1.1.5 – 202 – Test Specification © IO-Link

### 1573 7.10.27 Offset Time – valid

### 1574 Table 150 defines the test conditions for this test case.

### 1575 Table 150 – Offset Time – valid

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0134

Name TCD_DLIC_DEFP_OFFTIMEVALID

Purpose (short) Behavior of OffsetTime with valid values

Equipment under test (EUT) Device with adequate reference in IODD

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2.20, Table B.11

Configuration / setup Device-Tester-Unit 1576

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The test verifies whether the value range for OffsetTime is correct.

Precondition DTU: Communication EUT: OPERATE at maximum MasterCycleTime

Procedure a) Write OffsetTime with "0" for time base and for multiplier (Index 0x0030) b) Read OffsetTime c) Repeat step a) and step b) with multiplier = "63" d) Repeat step a) to step c) with time base = "1" and time base = "2"

Test parameter –

Post condition – 1577

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Write response in a) and Read response in b)

2. Compare written value in a) with response value in b)

Test passed No negative responses, and Values in 2) are matching

Test not passed (examples) No response

Report For all three settings: OffsetTime valid response: <positive/negative> <ok | nok> OffsetTime valid matching values: <yes/no> <ok | nok>

1578

### Test Specification © IO-Link – 203 – Version V1.1.5

### 1579 7.10.28 Profile Parameter – Read access

### 1580 Table 151 defines the test conditions for this test case.

### 1581 Table 151 – Profile Parameter – Read access

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0136

Name TCD_DLIC_DEFP_PROFILEPARREAD

Purpose (short) Behavior of unexpected profile parameter Read access

Equipment under test (EUT) Device supporting no profile: IODD attribute "features/ProfileCharacteristic" is not implemented within the IODD or contains no entries

Test case version 1.2

Category / type Device application test: test to fail

Specification (clause) [1], see Annex B.2.21

Configuration / setup Device-Tester-Unit 1582

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test of Read access to Profile specific parameters

Precondition DTU: Communication EUT: OPERATE

Procedure a) Read Profile parameters from Index 0x0031 to 0x003F

Test parameter –

Post condition – 1583

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check each Read response

Test passed Negative response = IDX_NOTAVAIL (0x8011) upon each profile parameter

Test not passed (examples) No negative response on profile parameters, or Negative response ≠ IDX_NOTAVAIL (0x8011)

Report For all Read responses: Profile parameter response: <negative/positive> <ok | nok>

1584

1585

### Version V1.1.5 – 204 – Test Specification © IO-Link

### 1586 7.10.29 Profile Parameter – Write access

### 1587 Table 152 defines the test conditions for this test case.

### 1588 Table 152 – Profile Parameter – Write access

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0137

Name TCD_DLIC_DEFP_PROFILEPARWRITE

Purpose (short) Behavior of unexpected profile parameter Write access

Equipment under test (EUT) Device supporting no profile: IODD attribute "features/ProfileCharacteristic" is not implemented within the IODD or contains no entries

Test case version 1.2

Category / type Device application test: test to fail

Specification (clause) [1], see Annex B.2.21

Configuration / setup Device-Tester-Unit 1589

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test of Write access to Profile specific parameters

Precondition DTU: Communication EUT: OPERATE

Procedure a) Write profile parameters from index 0x0031 to 0x003F with <values>

Test parameter <values>: 0x0000

Post condition – 1590

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check each Write response

Test passed Negative response = IDX_NOTAVAIL (0x8011) upon each profile parameter

Test not passed (examples) No negative response on profile parameters, or Negative response ≠ IDX_NOTAVAIL (0x8011)

Report For all Write responses: Profile Parameter used response: <negative/positive> <ok | nok>

1591

1592

### Test Specification © IO-Link – 205 – Version V1.1.5

### 1593 7.10.30 Write access – Read only parameter

### 1594 Table 153 defines the test conditions for this test case.

### 1595 Table 153 – Write access – Read only parameter

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0140

Name TCD_DLIC_DEFP_WRITETOREADONLY

Purpose (short) Write access to Read only standard parameter

Equipment under test (EUT) Device with ISDU support and with adequate parameter reference in IODD

Test case version 1.3

Category / type Device application test: test to fail

Specification (clause) [1], see Annex B.2 and Annex C.2.8

Configuration / setup Device-Tester-Unit 1596

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test that Write access to a Read only standard parameter shows the correct response behavior

Precondition DTU: Communication EUT: OPERATE

Procedure a) First standard parameter marked "Read Only" b) Read standard parameter;provides parameter length c) Write <value> to standard parameter d) Repeat at b) with next standard parameter marked "Read Only"

Test parameter <value>: any standard parameter in IODD marked as "Read Only"

Post condition – 1597

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Write responses

Test passed Negative response = IDX_NOT_ACCESSIBLE (0x8023)

Test not passed (examples) No response

Report Upon all Write accesses to Read Only parameters: Parameter Read Only response: <ErrorType> <ok | nok>

1598

1599

### Version V1.1.5 – 206 – Test Specification © IO-Link

### 1600 7.10.31 Write access – Length too short

### 1601 Table 154 defines the test conditions for this test case.

### 1602 Table 154 – Write access – Length too short

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0141

Name TCD_DLIC_DEFP_WRITETOOSHORT

Purpose (short) Write access with invalid length (too short) to writable standard parameter

Equipment under test (EUT) Device with ISDU support and writeable standard parameters are referenced within the IODD

Test case version 1.1

Category / type Device application test: test to fail

Specification (clause) [1], see Annex B.2 and Annex C.2.13

Configuration / setup Device-Tester-Unit 1603

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test that Write access to a writable standard parameter with a too short parameter length shows the correct response behavior

Precondition DTU: Communication EUT: OPERATE

Procedure a) First standard parameter with no data type StringT and length > 1 octet b) Read standard parameter;provides parameter length c) Write <value> with reduced length to standard parameter d) Repeat at b) with next standard parameter with no StringT and length > 1 octet

Test parameter <value>: any writable standard parameter in IODD

Post condition – 1604

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Write responses

Test passed Negative response = VAL_LENUNDRUN (0x8034)

Test not passed (examples) No response or incorrect ErrorType

Report Upon all Write accesses with too short length to writable parameters: Parameter Write response: <ErrorType> <ok | nok>

1605

1606

### Test Specification © IO-Link – 207 – Version V1.1.5

### 1607 7.10.32 Write access – Length too long

### 1608 Table 155 defines the test conditions for this test case.

### 1609 Table 155 – Write access – Length too long

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0142

Name TCD_DLIC_DEFP_WRITETOOLONG

Purpose (short) Write access with invalid length (too long) to writable standard parameter

Equipment under test (EUT) Device with ISDU support and writeable standard parameters are referenced within the IODD

Test case version 1.1

Category / type Device application test: test to pass

Specification (clause) [1], see Annex B.2, and Annex C.2.8

Configuration / setup Device-Tester-Unit 1610

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test that Write access to a writable standard parameter with a too long parameter length shows the correct response behavior

Precondition DTU: Communication EUT: OPERATE

Procedure a) First standard parameter with no data type StringT and length > 1 octet b) Read standard parameter;provides parameter length c) Write <value> with extended length to standard parameter d) Repeat at b) with next standard parameter with no StringT and length > 1 octet

Test parameter <value>: any writable standard parameter in IODD

Post condition – 1611

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check Write responses

Test passed Negative response = VAL_LENOVRRUN (0x8033)

Test not passed (examples) No response or incorrect ErrorType

Report Upon all Write accesses with too long length to writeable parameters: Parameter Write response: <ErrorType> <ok | nok>

1612 1613 1614 1615 1616 1617 1618

### 1619 7.11 Block parameter

### 1620 7.11.1 General

### 1621 The manufacturer/vendor of a Device shall provide information about a possible Block Param-

### 1622 eter set enabling the performance of the following tests. This Block Parameter set shall comply

### 1623 with the requirements of the test cases 7.11.2 through 7.11.7.

1624

### Version V1.1.5 – 208 – Test Specification © IO-Link

### 1625 7.11.2 Block parameter – Download

### 1626 Table 156 defines the test conditions for this test case.

### 1627 Table 156 – Block parameter – Download

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0143

Name TCD_DSBP_APPL_BPDOWNLOAD

Purpose (short) Test of Block parameter Download

Equipment under test (EUT) Device with option "blockParameter" indicated in IODD

Test case version 1.1

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1628

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Test of Block parameter Download

Precondition DTU: SDCI communication EUT: No Block parameterization is active

Procedure a) Write SystemCommand "ParamDownloadStart" b) Write Block parameters defined in field Test parameter c) Write SystemCommand "ParamDownloadEnd"

Test parameter Parameter set defined in 7.7.1.1

Post condition – 1629

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step a) that no ErrorType has been received

2. Check during step b) that no ErrorType has been received

3. Check after step c) that no ErrorType has been received

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Write "ParamDownloadStart" <ok | nok> Write Block parameter <ok | nok> Write "ParamDownloadEnd" <ok | nok>

1630

1631

### Test Specification © IO-Link – 209 – Version V1.1.5

### 1632 7.11.3 Block parameter – Break by command

### 1633 Table 157 defines the test conditions for this test case.

### 1634 Table 157 – Block parameter – Break by command

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0144

Name TCD_DSBP_APPL_BPBREAKCMD

Purpose (short) Test break of Block parameter transfer per command

Equipment under test (EUT) Device with option "blockParameter" indicated in IODD

Test case version 1.1

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1635

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device shall discard any change of parameters when receiving a SystemCommand "ParamDownloadBreak"

Precondition DTU: SDCI communication EUT: no Block parameterization is active

Procedure a) Read one parameter (see field Test parameter) and buffer it as <value1> b) Write SystemCommand "ParamDownloadStart" c) Write parameter from step a) with <value2> ≠ <value1> d) Write SystemCommand "ParamDownloadBreak" e) Read parameter from step a) f) Write SystemCommand "ParamDownloadEnd" g Read parameter from step a)

Test parameter Parameter set defined in 7.7.1.1

Post condition – 1636

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that no ErrorType has been received

2. Check after step d) that no ErrorType has been received

3. Check after step e) that received value matches <value1>

4. Check after step f) that ErrorType 0x8036 has been received

5. Check after step g) that received value matches <value1>

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Write "ParamDownloadBreak" <ok | nok> Rollback to previous value after break <ok | nok> Write "ParamDownloadEnd" <ok | nok> Non-volatile storage after ParamDownloadEnd <ok | nok>

1637

1638

### Version V1.1.5 – 210 – Test Specification © IO-Link

### 1639 7.11.4 Block parameter – Break by reset

### 1640 Table 158 defines the test conditions for this test case.

### 1641 Table 158 – Block parameter – Break by reset

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0145

Name TCD_DSBP_APPL_BPBREAKRESET

Purpose (short) Test break of Block parameter transfer per reset

Equipment under test (EUT) Device with option "blockParameter" indicated in IODD

Test case version 1.1

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1642

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device shall discard any change of parameters if a reset occurs during parameterization

Precondition DTU: SDCI communication EUT: No Block parametrization is active

Procedure a) Read one Parameter (see field Test parameter) and buffer it as <value1> b) Write SystemCommand "ParamDownloadStart" c) Write parameter from step a) with <value2> ≠ <value1> d) Reset the Device either (descending priority based on availability)

- using MasterCommand "Fallback" or

- using SystemCommand "DeviceReset" or

- using power off/on cycle

e) Set Device into SDCI communication f) Read Parameter from step a) g) Write SystemCommand "ParamDownloadEnd" h) Read Parameter from step a)

Test parameter Parameter set defined in 7.7.1.1

Post condition – 1643

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that no ErrorType has been received

2. Check after step e) that Device communication is active

3. Check after step f) that received value matches <value1>

4. Check after step g) that ErrorType 0x8036 has been received

5. Check after step h) that received value matches <value1>

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Write "ParamDownloadBreak" <ok | nok> SDCI restart <ok | nok> Rollback to previous value after communication restart <ok | nok> Write "ParamDownloadEnd" <ok | nok> Non-volatile storage after ParamDownloadEnd <ok | nok>

1644

1645

### Test Specification © IO-Link – 211 – Version V1.1.5

### 1646 7.11.5 Block parameter – Illegal parameter write

### 1647 Table 159 defines the test conditions for this test case.

### 1648 Table 159 – Block parameter – Illegal parameter write

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0146

Name TCD_DSBP_APPL_BPBREAKILLPARAM

Purpose (short) Test break of Block parameter transfer by illegal parameter

Equipment under test (EUT) Device with option “blockParameter” indicated in IODD

Test case version 1.2

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1649

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device shall discard any change of parameters if an illegal parameter occurs during parameterization

Precondition DTU: SDCI communication EUT: no Block parametrization is active

Procedure a) Read one parameter (see Test parameter) and buffer it as <value1> b) Write SystemCommand "ParamDownloadStart" c) Write parameter from step a) with <value2> ≠ <value1> d) Write another parameter using an illegal structure (incorrect data length) e) Write SystemCommand "ParamDownloadEnd" f) Read parameter from step a)

Test parameter Parameter set defined in 7.7.1.1

Post condition – 1650

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that no ErrorType has been received

2. Check after step d) that ErrorType 0x8033 or 0x8034 has been received

3. Check after step e) that ErrorType 0x8041 has been received

4. Check after step f) that received parameter matches <value1>

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Write "ParamDownloadStart" <ok | nok> Device behavior upon illegal structure <ok | nok> Write "ParamDownloadEnd" <ok | nok> Rollback to previous value after inconsistent parameter block <ok | nok>

1651

1652

### Version V1.1.5 – 212 – Test Specification © IO-Link

### 1653 7.11.6 Block parameter – Break by double download

### 1654 Table 160 defines the test conditions for this test case.

### 1655 Table 160 – Block parameter – Break by double download

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0147

Name TCD_DSBP_APPL_BPBREAK2DOWNLOADS

Purpose (short) Test break of Block Parameter transfer by double SystemCommand "ParamDownloadStart"

Equipment under test (EUT) Device with option “blockParameter” indicated in IODD

Test case version 1.1

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1656

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device shall discard any change of parameters if Block parameterization is restarted via a new "ParamDownloadStart" during a running Block parameter transfer

Precondition DTU: SDCI communication EUT: no block parametrization is active

Procedure a) Read one parameter (see field Test parameter) and buffer it as <value1> b) Write SystemCommand "ParamDownloadStart" c) Write parameter from step a) with <value2> ≠ <value1> d) Write SystemCommand "ParamDownloadStart" e) Write all parameters in field Test parameter, except those used in step a) f) Write SystemCommand "ParamDownloadEnd" g Read parameter from step a)

Test parameter Parameter set defined in 7.7.1.1

Post condition – 1657

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that no ErrorType has been received

2. Check after step d) that no ErrorType has been received

3. Check during step e) that no ErrorType has been received

4. Check after step f) that no ErrorType has been received

5. Check after step g) that received value does not match <value1>

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Write "ParamDownloadStart" <ok | nok> Write second "ParamDownloadStart" while block is unfinished <ok | nok> Write Block parameter <ok | nok> Write "ParamDownloadEnd" <ok | nok> Rollback to previous values via second “ParamDownloadStart” <ok | nok>

1658

1659

### Test Specification © IO-Link – 213 – Version V1.1.5

### 1660 7.11.7 Block parameter – local locking

### 1661 Table 161 defines the test conditions for this test case.

### 1662 Table 161 – Block parameter – local locking

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0148

Name TCD_DSBP_APPL_BPBREAKLOCALLOCK

Purpose (short) Test locking of local parametrization during Block parameter transfer

Equipment under test (EUT) Device with option “blockParameter” indicated in IODD, and local parameterization capability (on-board)

Test case version 1.1

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1663

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Local access and change of parameterization shall be locked during Block parameterization

Precondition DTU: SDCI communication EUT: no block parametrization is active

Procedure a) Read one parameter from A) and buffer it as <value1> b) Read parameter B) and buffer it as <value2> c) Write SystemCommand "ParamDownloadStart" d) Write parameter from step a) with <value2> ≠ <value1> e) Try to change parameter B) via local parameterization f) Write SystemCommand "ParamDownloadEnd" g) Read parameter from step a) h) Read parameter B)

Test parameter A) Parameter set defined in 7.7.1.1 B) Parameter in A) that can be changed by local parametrization (on-board) and via SDCI communication Post condition – 1664

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step c) that no ErrorType has been received

2. Check during step e) that the Device denies the trial of local parameterization

3. Check after step f) that no ErrorType has been received

4. Check after step g) that received value does not match <value1>

5. Check after step h) that received value matches <value2>

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Write "ParamDownloadStart" <ok | nok> Inhibited local parametrization <ok | nok> Write "ParamDownloadEnd" <ok | nok> Non-volatile storage after "ParamDownloadEnd" <ok | nok> Rejection of local changes <ok | nok>

1665

### Version V1.1.5 – 214 – Test Specification © IO-Link

### 1666 7.11.8 ParameterManager – Unexpected commands in Idle

### 1667 Table 162 defines the test conditions for this test case.

### 1668 Table 162 – ParameterManager – Unexpected commands in Idle

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0324

Name TCD_DSBP_APPL_UNEXPECTEDINIDLE

Purpose (short) Test of unexpected commands in state Idle of ParameterManager

Equipment under test (EUT) Device

Test case version 1.0

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, 10.6.11, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1669

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device shall issue the correct ErrorType after receiving an unexpected block ending command while ParameterManager is in state Idle.

Precondition DTU: SDCI communication EUT: No block parametrization is active

Procedure Write SystemCommand "ParamUploadEnd" Write SystemCommand "ParamBreak" Write SystemCommand "ParamDownloadEnd"

Test parameter "blockParameter" <true / false> indicated in IODD

Post condition – 1670

TEST CASE RESULTS CHECK / REACTION

Evaluation After steps a) to c), check received ErrorType

Test passed ErrorType = 0x8036 if blockParameter <true>, OR ErrorType = 0x8035 if blockParameter <false>

Test not passed (examples) No response or incorrect ErrorTypes

Report Unexpected actions in ParameterManager state Idle <ok | nok>

1671

### Test Specification © IO-Link – 215 – Version V1.1.5

### 1672 7.11.9 ParameterManager – Write request during Upload

### 1673 Table 163 defines the test conditions for this test case.

### 1674 Table 163 – ParameterManager – Write request during Upload

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0325

Name TCD_DSBP_APPL_WRITEINUPLOAD

Purpose (short) Test of reaction on write accesses during an active Block Upload

Equipment under test (EUT) Device with option "blockParameter" indicated in IODD

Test case version 1.0

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, 10.6.11, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1675

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device shall not accept any write accesses to parameters except via SystemCommands during an active Block Upload. The SystemCommand is already tested in other test cases.

Precondition DTU: SDCI communication EUT: No block parametrization is active

Procedure a) Write SystemCommand "ParamUploadStart" b) Write any parameter which is not excluded from DataStorage c) Write SystemCommand "ParamUploadEnd" d) Write any parameter which is not excluded from DataStorage

Test parameter One parameter of the parameter sets defined in 7.7.1.1

Post condition – 1676

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that ErrorType 0x8022 has been received

2. Check after step d) that no ErrorType has been received

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Write restriction during Block parameter Upload <ok | nok>

1677

### Version V1.1.5 – 216 – Test Specification © IO-Link

### 1678 7.11.10 ParameterManager – Read requests during Download

### 1679 Table 164 defines the test conditions for this test case.

### 1680 Table 164 – ParameterManager – Read requests during Download

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0326

Name TCD_DSBP_APPL_READINDOWNLOAD

Purpose (short) Test of reaction on read accesses during active Download

Equipment under test (EUT) Device with option "blockParameter" indicated in IODD

Test case version 1.0

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, 10.6.11, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1681

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device shall not accept any Read accesses to parameters during an active Block Download.

Precondition DTU: SDCI communication EUT: No Block parametrization is active

Procedure a) Write SystemCommand "ParamDownloadStart" b) Read Test parameter c) Read DataStorageIndex, Index 3, SubIndex 0 d) Write SystemCommand "ParamDownloadEnd" e) Read Test parameter

Test parameter One parameter of the parameter sets defined in 7.7.1.1

Post condition – 1682

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that ErrorType 0x8022 has been received

2. Check after step c) that no ErrorType has been received

3. Check after step e) that no ErrorType has been received

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Read restriction during Block parameter Download <ok | nok>

1683

### Test Specification © IO-Link – 217 – Version V1.1.5

### 1684 7.11.11 ParameterManager – Unexpected commands during Upload

### 1685 Table 165 defines the test conditions for this test case.

### 1686 Table 165 – ParameterManager – Unexpected commands during Upload

Identification (ID) SDCI_TC_0327

Name TCD_DSBP_APPL_UNEXPINUPLOAD

Purpose (short) Test of reaction on unexpected SystemCommands during active Upload

Equipment under test (EUT) Device with option "blockParameter" indicated in IODD

Test case version 1.0

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, 10.6.11, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1687

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) During an active Block Upload, a Device shall react on unexpected SystemCommands, abort the ongoing process and switch to the requested state.

Precondition DTU: SDCI communication EUT: - No Event pending or active

- DataStorage of the Device is inactive and DS_UPLOAD_FLAG is "0"

- No Block parametrization is active

Procedure a) Write SystemCommand "ParamUploadStart" b) Write Test parameter A)

c) Write SystemCommand "ParamUploadStart" d) Write Test parameter A) e) Write SystemCommand "ParamDownloadEnd" f) Write Test parameter A)

If "dataStorage" = <true> g) Write SystemCommand "ParamUploadStart" h) Write SystemCommand "ParamDownloadStore" i) Wait for Event (max 1s) and read DataStorage Index j) Write DS_Command "DS_UploadEnd"

Test parameter A) One parameter of the parameter sets defined in 7.7.1.1 B) dataStorage <true / false> indicated in IODD

Post condition – 1688

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that ErrorType 0x8022 has been received

2. Check after step d) that ErrorType 0x8022 has been received

3. Check after step f) that no ErrorType has been received

If "dataStorage" = <true>

4. Check after step i) that Event with code DS_UPLOAD_REQ has been received

and DS_UPLOADREQ = "1"

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Reaction on unexpected commands in Upload <ok | nok>

1689

### Version V1.1.5 – 218 – Test Specification © IO-Link

### 1690 7.11.12 ParameterManager – Unexpected switches Upload/Download

### 1691 Table 166 defines the test conditions for this test case.

### 1692 Table 166 – ParameterManager – Unexpected switches Upload/Download

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0328

Name TCD_DSBP_APPL_SWITCHSTATES

Purpose (short) Test of reaction on unexpected switches between Upload and Download

Equipment under test (EUT) Device with option "blockParameter" indicated in IODD

Test case version 1.0

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, 10.6.11, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1693

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Any Upload or Download shall be interrupted by the start of the opposite action.

Precondition DTU: SDCI communication EUT: No Block parameterization is active

Procedure a) Write SystemCommand "ParamUploadStart" b) Write Test parameter

c) Write SystemCommand "ParamDownloadStart" d) Read Test parameter e) Write SystemCommand "ParamUploadStart" f) Write Test parameter g) Write SystemCommand "ParamUploadEnd"

Test parameter One parameter of the parameter sets defined in 7.7.1.1

Post condition - 1694

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that ErrorType 0x8022 has been received

2. Check after step d) that ErrorType 0x8022 has been received

3. Check after step f) that ErrorType 0x8022 has been received

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Switching between Upload and Download <ok | nok>

1695

### Test Specification © IO-Link – 219 – Version V1.1.5

### 1696 7.11.13 ParameterManager – Upload interrupted by reset

### 1697 Table 167 defines the test conditions for this test case.

### 1698 Table 167 – ParameterManager – Upload interrupted by reset

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0329

Name TCD_DSBP_APPL_UPBREAKRESET

Purpose (short) Test of Block Parameter Upload transfer interrupted per reset

Equipment under test (EUT) Device with option "blockParameter" indicated in IODD

Test case version 1.0

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, 10.6.11, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1699

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Any Upload can be interrupted by a communication reset and Block parameterization shall be aborted in this case.

Precondition DTU: SDCI communication EUT: No block parametrization is active

Procedure a) Write SystemCommand "ParamUploadStart" b) Write Test parameter

c) Reset the Device either (descending priority based on availability)

- using MasterCommand "Fallback" or

- using SystemCommand "DeviceReset" or

- using power off/on cycle reset

d) Set Device to SDCI communication e) Write Test parameter

Test parameter One parameter of the parameter sets defined in 7.7.1.1

Post condition - 1700

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step b) that ErrorType 0x8022 has been received

2. Check after step e) that no ErrorType has been received

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Communication interruption during Upload <ok | nok>

1701

### Version V1.1.5 – 220 – Test Specification © IO-Link

### 1702 7.11.14 ParameterManager – UploadEnd during Download

### 1703 Table 168 defines the test conditions for this test case.

### 1704 Table 168 – ParameterManager – UploadEnd during Download

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0330

Name TCD_DSBP_APPL_DNENDBYUPLOAD

Purpose (short) Reaction on Upload commands while in Download, discarding written parameter

Equipment under test (EUT) Device with option "blockParameter" indicated in IODD

Test case version 1.0

Category / type Device Block parameter test: test to pass

Specification (clause) [1], 10.3.2, 10.3.5, 10.6.11, Table 97, Table 98, Table B.9

Configuration / setup Device-Tester-Unit 1705

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Parameter values of any Download shall be discarded when receiving an Upload- Start or UploadEnd command.

Precondition DTU: SDCI communication EUT: No Block parameterization is active Procedure a) Read Test parameter A) b) Write SystemCommand “ParamDownloadStart” c) Write Test parameter value A) or B), whichever is different from step a)

d) Write SystemCommand “ParamUploadStart” e) Read parameter from c) f) Write SystemCommand “ParamDownloadStart” g) Write Test parameter value A) or B), whichever is different from step a) h) Write SystemCommand “ParamUploadEnd” i) Read parameter from c)

Test parameter A) One parameter of the parameter sets defined in 7.7.1.1 with value of set 1 B) Same as parameter in A) with value of set 2

Post condition - 1706

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check after step e) that read value equals original value of step a)

2. Check after step i) that read value equals original value of step a)

Test passed All evaluation steps with positive result

Test not passed (examples) No response or any evaluation negative

Report Download break by any Upload command <ok | nok>

1707 1708 1709 1710 1711 1712 1713

### 1714 7.12 Test report summary of the Device protocol tests

### 1715 The template is defined by the Device-Tester. The complete test report shall present at least

### 1716 the information of the report fields of the test cases for the Device protocol tests.

1717

1718

Test Specification © IO-Link – 221 – Version V1.1.5

1719 8 IODD-related tests

1720 8.1 Overview

1721 The IODD test focuses on four major issues. The first issue is the test of a Device's IODD file 1722 with the help of a so-called IODD checker tool. IO-Link defines the parsers such an IODD 1723 checker tool shall use for IODD schema consistency checks. It also defines a set of business 1724 rules for the IODD check (see 8.2.1).

1725 The second issue focuses on the IODD interpreter tool. This test shall ensure, that the "Port 1726 and Device Configuration Tool" (PDCT) of the Master is able to provide all the IODD definitions 1727 in the correct manner (see 8.2.2).

1728 The third issue is the test whether the parameters defined in the IODD are accessible within 1729 the Device (parameter verification test). These test cases are specified in 8.3.

1730 The fourth issue is the test of complex system functions such as diverse reset possibilities that 1731 are indicated within the IODD. These test cases are specified in 8.4.

1732 For all these tests, the Master shall be configured with inspection level "type compatible".

1733 8.2 Schema test via an IODD checker tool and IODD interpreter tests

1734 8.2.1 IODD Checker

1735 The organization referenced in D makes available an IODD checker tool ("Checker") for free 1736 download from its web server. It is mandatory for each and every IODD associated with a Device 1737 to pass the test with this Checker. The Device's manufacturer declaration shall state the suc- 1738 cessful result of the test.

1739 The requirements for IODD testing had been specified in previous versions of this document 1740 but was shifted to the IODD specification [3] for practical reasons.

1741 8.2.2 IODD interpreter tool

1742 A Master-Tester-System shall provide a so-called fictive IODD with critical constellations of 1743 parameters, which are supported by the Master-Tester-Unit (MTU) playing the role of a Device. 1744 The IODD interpreter tools associated or related to a particular Master can be tested with the 1745 help of this IODD. Clause 5.5 describes how these tests can be performed.

1746

### Version V1.1.5 – 222 – Test Specification © IO-Link

### 1747 8.3 Parameter verification test

### 1748 8.3.1 IODD identification

### 1749 Table 169 defines the test conditions for this test case.

### 1750 Table 169 – IODD identification

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0149

Name TCD_IODD_PARV_IDENT

Purpose (short) Device matches the associated IODD

Equipment under test (EUT) Device and associated IODD

Test case version 1.0

Category / type IODD parameter verification test: test to pass

Specification (clause) [3], 7.4 and 7.4.1; [1], B.1.8, B.1.9 and B.2.11

Configuration / setup Device-Tester-Unit 1751

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Verify that Device matches the associated IODD. DTU reads Device parameters VendorID, DeviceID, and ProductID, and compares with IODD node descriptions. For further tests, read of DeviceID and ProductID are sufficient for an IODD association check

Precondition DTU: SDCI communication EUT: Device is communicating, associated IODD available in machine readable form

Procedure a) Read VendorID, DeviceID, ProductID from DPP1 of the Device b) Read corresponding entries from IODD;see Test parameter c) Read DeviceVariantCollection from IODD;see Test parameter

Test parameter IODD: DeviceIdentity/@vendorId, DeviceIdentity/@deviceId, DeviceVariant/@productId

Post condition – 1752

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check if IDs match the entries in IODD's Device identity after b)

2. Check if ProductID is listed in the DeviceVariantCollection of the IODD after c)

Test passed All IDs from Device found in IODD

Test not passed (examples) Any of the ID from Device not found in IODD (not matching)

Report VendorID: <value> <ok | nok> DeviceID: <value> <ok | nok> ProductID: <value> <ok | nok> ProductID listed in DeviceVariantCollection: <yes/no> <ok | nok>

1753

1754

### Test Specification © IO-Link – 223 – Version V1.1.5

### 1755 8.3.2 IODD communication parameter verification

### 1756 Table 170 defines the test conditions for this test case.

### 1757 Table 170 – IODD communication parameter verification

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0150

Name TCD_IODD_PARV_COMPROFILE

Purpose (short) Device's communication parameters match corresponding values in IODD

Equipment under test (EUT) Device and associated IODD

Test case version 1.0

Category / type IODD parameter verification test: test to pass

Specification (clause) [3], 7.6; [1], B.1.3, B.1.5 and B.1.6

Configuration / setup Device-Tester-Unit 1758

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Verify that Device's communication parameters match values within the associated IODD. DTU reads Device parameters MinCycleTime, RevisionID, and ProcessDataIn and compares with IODD node descriptions.

Precondition DTU: SDCI communication EUT: Device is communicating; associated IODD available in machine readable form VendorID and ProductID of the Device match values in IODD

Procedure a) Read MinCycleTime, RevisionID, ProcessDataIn (address 0x05, bit 6 – SIO supported) from DPP1 of the Device b) Read corresponding entries from IODD;see Test parameter

Test parameter IODD: CommNetworkProfile/@iolinkRevision, TransportLayers/PhysicalLayer/@minCycleTime, PhysicalLayer/@sioSupported

Post condition – 1759

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Compare values in DPP1 with corresponding entries in IODD after b)

Test passed Values are matching

Test not passed (examples) Values do not match

Report MinCycleTime: <value> <ok | nok> RevisionID: <value> <ok | nok> SIO supported: <yes/no> <ok | nok>

1760

1761

### Version V1.1.5 – 224 – Test Specification © IO-Link

### 1762 8.3.3 IODD parameter read verification

### 1763 Table 171 defines the test conditions for this test case.

### 1764 Table 171 – IODD parameter read verification

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0151

Name TCD_IODD_PARV_READVERIFY

Purpose (short) Verify access rights, structures, and data contents of Read parameters

Equipment under test (EUT) Device and associated IODD

Test case version 1.1

Category / type IODD parameter verification test: test to pass

Specification (clause) [3], 7.5.4; [1], Table C.1, C.2.2 to C.2.19, F.2.2 to F.2.9, F.3.2, F.3.3

Configuration / setup Device-Tester-Unit 1765

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Verify that all parameters in the IODD with Read access can be read from the Device, have a correct structure and valid data content. The length of an acquired parameter is used as measure for its structure since the lengths of parameters depend on their data. The validity of the data content is only checked for parameters of type StringT. The Tests shall be done by access with sub index == 0, and by subindex <>0 if the type of the variable is Record_T, regardless if subindexAccessSupported is true or false.

Precondition DTU: SDCI communication EUT: Device is communicating; associated IODD available in machine readable form; VendorID and ProductID of the Device match values in IODD

Procedure a) Get first parameter with Read access from IODD b) Read parameter with Subindex = 0 access c) Read Parameter via all defined SubIndices d) Repeat from b) with next parameter with Read access from IODD until last one

Test parameter –

Post condition – 1766

TEST CASE RESULTS CHECK / REACTION

Evaluation For each acquired parameter:

1. Check ErrorType after b)

2. Read parameter with Subindex = 0 access

3. Read Parameter via all defined SubIndices

4. Repeat from b) with next parameter with Read access from IODD until last

one

Test passed

1. No ErrorType or ErrorType 0x8020, 0x8021, or 0x8022

2. No ErrorType or ErrorType 0x8020, 0x8021, or 0x8022

3. ErrorType 0x8012 or 0x8023

4. Length check OK

5. String content check OK

Test not passed (examples) Any evaluation failed

Report For each and every parameter with Read access in the IODD: No Error: <yes/no> <ok | nok> Permitted Error: <ErrorType> <ok | nok>

### String content check OK (if applicable)

1767

1768

### Test Specification © IO-Link – 225 – Version V1.1.5

### 1769 8.3.4 IODD parameter write verification

### 1770 Table 172 defines the test conditions for this test case.

### 1771 Table 172 – IODD parameter write verification

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0152

Name TCD_IODD_PARV_WRITEVERIFY

Purpose (short) Test verifies index space and value ranges defined within the Device's IODD

Equipment under test (EUT) Device and associated IODD

Test case version 1.1

Category / type IODD parameter verification test: test to pass

Specification (clause) [3], 7.5.4; [1], Table C.1, C.2.2 to C.2.19, F.2.2 to F.2.9, F.3.2, F.3.3

Configuration / setup Device-Tester-Unit 1772

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Device-Tester writes dedicated IODD parameters to the Device. Verify that all parameters in the IODD can be accessed. Test for semantics (device specific technology) is not included. The individual rules for applicable values to be written are defined depending on the data types (for example string, integer, enum), see section "Test parameter". The Tests shall be done by access with sub index == 0, and by subindex <>0 if the type of the variable is Record_T, regardless if subindexAccessSupported is true or false.

Precondition DTU: SDCI communication EUT: Device is communicating; associated IODD available in machine readable form VendorID and ProductID of the Device match values in IODD

Procedure a) Store parameter content of all parameters to be tested

b) Write parameter content depending on data type using subindex 0

c) Write parameter content parts depending on data type using all defined subindeces.

d) Repeat b) and c) with next test parameter e) write content from a) to all tested parameters

Test parameter Parameters with Read/Write access from IODD

For every <value> to be written, the following rules for the data types shall apply:

- String: filled with blanks

- INT, UINT, FLOAT: maximum value of the permitted range minus one unit

- BOOL: true

- Time: 2011-04-18 12:00:00 UTC

- Timespan: 1 s

- Enum (single value): first single value

Post condition - 1773

TEST CASE RESULTS CHECK / REACTION

Evaluation For each written parameter:

1. Check ErrorType after b)

2. Check ErrorType after c) in case of subindexAccessSupported=”true”

3. Check ErrorType after c) in case of subindexAccessSupported=”false”

Test passed

1. No ErrorType or ErrorType 0x8020, 0x8021, 0x8022, 0x8030, 0x8031, 0x8032

2. No ErrorType or ErrorType 0x8020, 0x8021, 0x8022, 0x8030, 0x8031, 0x8032

3. ErrorType 0x8012 or 0x8023

### Version V1.1.5 – 226 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

Test not passed (examples) Any evaluation failed

Report For every parameter with Write access in the IODD: No Error: <yes/no> <ok | nok> Permitted Error: <ErrorType> <ok | nok>

1774

### Test Specification © IO-Link – 227 – Version V1.1.5

### 1775 8.3.5 IODD parameter Index/Subindex consistency

### 1776 Table 173 defines the test conditions for this test case.

### 1777 Table 173 – IODD parameter Index/Subindex consistency

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0157

Name TCD_IODD_PARV_INDEXCONSISTENT

Purpose (short) Test the consistency between Indices and Subindices for IODD parameters

Equipment under test (EUT) Device and associated IODD

Test case version 1.1

Category / type IODD parameter verification test: test to pass

Specification (clause) [3], 7.5.4; [1], A.5.4, F.3.2, F.3.3

Configuration / setup Device-Tester-Unit 1778

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Device-Tester tests if reading or writing parameters via Subindex has the same result as reading or writing via Index. Test shall be repeated for all parameters which are:

- not dynamic

- readable and writeable

- subindex access is allowed

For each parameter, test shall be repeated for all subindices which are read-able and writeable

Precondition DTU: SDCI communication EUT: Device is communicating; associated IODD available in machine readable form VendorID and ProductID of the Device match values in IODD This test only for non-dynamic parameters with complex data type and "subindexAccessSupported = true" within the IODD

Procedure a) Find a valid value <value1> for subindex 0

b) Write <value1> to subindex 0

c) Read subindex 0 and store it to <value2>

d) Extract value of subindex n from <value1> and store it to <value3>

e) Read subindex n and store it to <value4>

f) Find valid value <value5> for subindex n which is different to <value3>

g) Write <value5> to subindex n

h) Read subindex 0 and store it to <value6>

i) Extract value of subindex n from <value6> and store it to <value7>

j) Read subindex n and store it to <value8>

Test parameter For <value1> and <value5> use values from parameter sets defined in 6.7.1.1

Post condition – 1779

TEST CASE RESULTS CHECK / REACTION

Evaluation After step c) compare <value1> with <value2>. Values must be equal.

After step e) compare <value3> with <value4>. Values must be equal.

After step i) compare <value5> with <value7>. Values must be equal.

After step j) compare <value5> with <value8>. Values must be equal.

### Version V1.1.5 – 228 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

Test passed All evaluations are positive

Test not passed (examples) Any evaluation failed

Report For every parameter with Subindex access in the IODD: Parameter consistency: <value2/value1> <ok | nok> Parameter consistency: <value2/value3> <ok | nok> 1780

Test Specification © IO-Link – 229 – Version V1.1.5

1781 8.4 Functional system tests (IODD indication)

1782 8.4.1 General

1783 The availability of some more complex system functions of a Device is indicated in its IODD. 1784 The corresponding functional system tests are specified in this clause.

1785

### Version V1.1.5 – 230 – Test Specification © IO-Link

### 1786 8.4.2 IODD – functional verification of "Parameter (write) access lock"

### 1787 Table 174 defines the test conditions for this test case.

### 1788 Table 174 – IODD – functional verification of "Parameter (write) access lock"

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0156

Name TCD_IODD_PARV_ACCESSLOCK

Purpose (short) Test functional behavior of parameter access locking (conditional)

Equipment under test (EUT) Device with feature "Parameter (write) access" indicated within IODD

Test case version 1.1

Category / type IODD functional system test: test to pass

Specification (clause) [1], 10.6.5, B.2.4, C.2.1, Table C.1

Configuration / setup Device-Tester-Unit 1789

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Access to parameter write values is locked if feature "Device Access Lock" is implemented. Only parameters with attribute: accessRights = "rw" or accessRights = "wo" in IODD are tested.

Precondition DTU: OPERATE EUT: Device is in SDCI communication mode, VendorID and DeviceID match with IODD

Procedure a) Write value "0x0001" to Index "DeviceAccessLocks" (= parameter access locked) b) Select parameter with Write access within IODD c) Write <value> (see Test parameter) to selected parameter in b) d) Write value "0x0000" to Index "DeviceAccessLocks"

Test parameter <value> to be defined by manufacturer

Post condition – 1790

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Response is negative and ErrorType = 0x8023 (= access denied)

2. Procedure d) results in a positive response

Test passed Evaluations 1) and 2) are true

Test not passed (example) Evaluation 1) or 2) are false

Report Parameter (from evaluation 1): <index>, <ErrorType> <ok | nok> DeviceAccessLocks (from evaluation 2) <ok | nok> Warning: <yes/no>

1791

### Test Specification © IO-Link – 231 – Version V1.1.5

### 1792 8.4.3 IODD – functional verification of "Device reset"

### 1793 Table 175 defines the test conditions for this test case.

### 1794 Table 175 – IODD – functional verification of "Device reset"

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0316

Name TCD_IODD_PARV_DEVICERESET

Purpose (short) Test functional behavior of SystemCommand "Device reset" (conditional)

Equipment under test (EUT) Device with feature "Device reset" indicated in IODD

Test case version 1.1

Category / type IODD functional system test: test to pass

Specification (clause) [1], 10.7.2, Table 101, B.2.2

Configuration / setup Device-Tester-Unit 1795

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Parameter and reset behavior shall be as specified after a reset triggered by the SystemCommand "Device reset".

Precondition DTU: OPERATE EUT: Device is in SDCI communication mode; VendorID and DeviceID match IODD

Procedure a) Provide input field or configuration option for variable <time> representing the maximum Device time after System command Device Reset to restart communication (in seconds) b) Select parameter from IODD fulfilling the conditions in "Test parameter" c) Read selected parameter from b) and store values in array <parvalue_a> d) Read DID (Device ID) and store value in <didvalue_a> e) Write SystemCommand "Device reset" (128 / 0x80) f) Wait <time> g) Check if a communication startup sequence has been triggered h) Read RID (RevisionID) i) Read DID (DeviceID) and store value in <didvalue_b> j) Check if an Event has been triggered (mode "Event appears k) Read parameter DeviceStatus and store value to <status_b>") if DeviceStatus is marked as implemented in IODD l) Read selected parameter from b) and store values in array <parvalue_b>

Test parameter Only parameters in IODD are tested

- with attribute "accessRights = "rw", and

- which are not marked with "excludedFromDataStorage="true" in the IODD

Post condition – 1796

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Procedure g) shows exactly one communication startup sequence

2. Result of procedure h) matches with IODD (CommNetworkProfile:

iolinkRevision = "Vx.x")

3. Values of <didvalue_a> match values of <didvalue_b>

4. Procedure j) shows at least one event of mode "Event appears" or value of

<status_b> is equal ‘0 (0x00)’ ( Skip this step, if DeviceStatus is not marked as implemented in IODD) For each received parameter (from procedure b):

5. Response is positive

6. Values of <parvalue_a> match values of <parvalue_b>

Test passed All evaluations 1) to 6) are true

Test not passed (example) Any of the evaluations 1) to 6) is false

Report Communication from evaluation 1): <value> <ok | nok> RevisionID from evaluation 2): <value> <ok | nok> DeviceID from evaluation 3): <value> <ok | nok> DeviceStatus: <value> (from procedure k), <event> (from procedure j) <ok | nok>

Version V1.1.5 – 232 – Test Specification © IO-Link

For each received parameter (from evaluation 5) and 6)): Parameter: <Index>, <parvalue_a>, <parvalue_b> <ok | nok> 1797

### Test Specification © IO-Link – 233 – Version V1.1.5

### 1798 8.4.4 IODD – functional verification of "Application reset"

### 1799 Table 176 defines the test conditions for this test case.

### 1800 Table 176 – IODD – functional verification of "Application reset"

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0317

Name TCD_IODD_PARV_APPLRESET

Purpose (short) Test functional behavior of SystemCommand "Application reset" (conditional)

Equipment under test (EUT) Device with feature "Application reset" indicated in IODD

Test case version 1.2

Category / type IODD functional system test: test to pass

Specification (clause) [1], 10.7.3, Table 101, B.2.2

Configuration / setup Device-Tester-Unit 1801

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Parameter and reset behavior shall be as specified after a reset triggered by the SystemCommand "Application reset"

Precondition DTU: Data storage is disabled EUT: Device is in SDCI communication mode, VendorID and DeviceID match IODD

Procedure a) Select parameter from IODD fulfilling the conditions in "Test parameter" and excluding parameter ApplicationSpecificTag, FunctionTag, LocationTag b) If supported, read parameter ApplicationSpecificTag, FunctionTag and LoctionTag and store responses in <tagvalue_a> c) Read parameter DID (DeviceID) and RID (RevisionID) and store values in <idvalue_a> d) Write SystemCommand "Application reset" (129 / 0x81) e) Wait 5 s f) Check if a communication startup sequence has been triggered g) Wait for event DS_UPLOAD_REQ h) Check DS_UPLOAD_FLAG in parameter DataStorageIndex i) Read parameter DID (Device ID) and RID (Revision ID) and store values in <idvalue_b> j) If supported, read parameter ApplicationSpecificTag, FunctionTag and LocationTag and store responses in <tagvalue_b> k) Read selected parameter from a) and store values in array <parvalue_b> l) Write 0x04 (DS_DownloadEnd) to Index 3, subindex 1

Test parameter Only parameters in IODD are tested

- with attribute "accessRights = "rw",

- with attribute “defaultValue”

Post condition – 1802

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Procedure f) shows no communication startup sequence

2. Values of <idvalue_a> and <idvalue_b> are equal

3. Values of <tagvalue_a> and <tagvalue_b> are equal

4. Check whether Event DS_UPLOAD_REQ was raised after SystemCommand “Ap-

plication reset”

5. Value of procedure h) (DS_UPLOAD_FLAG) is equal ‘1’ (DS_UPLOAD_REQ

pending) if DS is supported, or is eq ‘0’ if DS is not supported For each received parameter (from procedure a)):

6. Response is positive

7. Values of <parvalue_b> match corresponding assigned “defaultValue Value” if

available in IODD Hint evaluation 7) : Results are only logged

Test passed All evaluations 1) to 6) are true

Test not passed (example) At least one of the evaluations 1) to 6) is false

### Version V1.1.5 – 234 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

Report Communication (from evaluation 1)) <ok | nok> RevisionID/DeviceID (from evaluation 2)) <ok | nok> Identification (from evaluation 3)) <ok | nok> DataStorage (from evaluation 4)) <ok | nok>

1803

### Test Specification © IO-Link – 235 – Version V1.1.5

### 1804 8.4.5 IODD – functional verification of "Restore factory settings"

### 1805 Table 177 defines the test conditions for this test case.

### 1806 Table 177 – IODD – functional verification of "Restore factory settings"

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0155

Name TCD_IODD_PARV_FACTORYSETTINGS

Purpose (short) Test functional behavior of SystemCommand "Restore factory settings" (conditional)

Equipment under test (EUT) Device with feature "Restore factory settings" indicated in IODD

Test case version 1.2

Category / type IODD functional system test: test to pass

Specification (clause) [1], 10.7.4, Table 101, B.2.2

Configuration / setup Device-Tester-Unit 1807

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Parameter and reset behavior shall be as specified after a reset triggered by the SystemCommand "Restore factory settings"

Precondition DTU: Data storage is disabled, Inspection level is set to “compatible” EUT: Device is in SDCI communication mode, VendorID and DeviceID match IODD

Procedure a) Select parameter from IODD, which fulfill the conditions in "Test parameter" b) Read parameter DeviceStatus and store value in <status_a> c) Write SystemCommand " ParamDownloadStore" (5 / 0x05) d) Write SystemCommand "Restore factory setting" (130 / 0x82)

e) Check if an Event has been triggered (mode "Event disappears") f) Wait 5 seconds g) Check if a communication startup sequence has been triggered h) Check DS_UPLOAD_FLAG in parameter DataStorageIndex i) Read parameter DID (DeviceID) and RID (RevisionID) and store to <idvalue_b> j) Check if an Event has been triggered (mode "Event appears") k) Read parameter DeviceStatus and store value in <status_b> l) Read selected parameter from a) and store values in array <parvalue_b>

Test parameter Only parameters in IODD are tested

- with attribute "accessRights = "rw", and

- which are not "excludedFromDataStorage"

and

- with attribute: defaultValue

Post condition – 1808

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Procedure g) shows no or exactly one communication startup sequence

2. If <status_a> is ≠ "0 (0x00)", procedure e) shows at least one Event of mode

"Event disappears", ( if a communication startup occured skip this evaluation )

3. Values of <idvalue_b> match default values of IODD

RID: (CommNetworkProfile→iolinkRevision="Vx.x")

4. Value of procedure h) (DS_UPLOAD_FLAG) = "0" (no DS_UPLOAD_REQ)

5. Procedure j) shows at least one Event of mode "Event appears"

or value of <status_b> = "0 (0x00)" For each received parameter (from procedure a)):

6. Response is positive

7. Values of <parvalue_b> match corresponding assigned "defaultValue" if available

in IODD

Test passed All evaluations 1) to 7) are true (if a communication startup occured skip evaluation 2)

Test not passed (example) At least one of the evaluations 1) to 7) is false

### Version V1.1.5 – 236 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

Report Communication (from evaluation 1)) <ok | nok> Diagnosis clear (from evaluation 2)) <ok | nok> RevisionID/DeviceID (from evaluation 3)) <ok | nok> DataStorage (from evaluation 4)) <ok | nok> DeviceStatus (from evaluation 5)): <value>, <event> <ok | nok> For each received parameter (from evaluation 6) and 7)): Parameter: <Index>, <parvalue_a>, <parvalue_b> <ok | nok>

1809 1810

### Test Specification © IO-Link – 237 – Version V1.1.5

### 1811 8.4.6 IODD – functional verification of "Back-to-box"

### 1812 Table 178 defines the test conditions for this test case.

### 1813 Table 178 – IODD – functional verification of "Back-to-box"

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0318

Name TCD_IODD_PARV_BACKTOBOX

Purpose (short) Test functional behavior of SystemCommand "Back-to-box" (conditional)

Equipment under test (EUT) Device with feature "Back-to-box" indicated in IODD

Test case version 1.1

Category / type IODD functional system test: test to pass

Specification (clause) [1], 10.7.5, Table 101, B.2.2

Configuration / setup Device-Tester-Unit In case of the DeviceId is not equal to the DefaultDeviceId (see System Spec Table

101. , the DefaultIODD must be provided

1814

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Parameter and reset behavior shall be as specified after a reset triggered by the SystemCommand "Back-to-box"

Precondition DTU: Data storage is disabled EUT: Device is in SDCI communication mode, VendorID and DeviceID match IODD

Procedure a) Select parameter from <DefaultIIODD> fulfilling the conditions in "Test parameter" b) Call SystemCommand "ParamDownloadStart" if Device supports Block Parameterization c) Write parameter set 1 into the Device because it is different to factory reset d) Write SystemCommand " ParamDownloadStore" (5 / 0x05) e) Write SystemCommand "Back-to-box" (131 / 0x83) f) Wait 5 s g) Check communication h) Apply power cycle (power off, wait 5 s, power on Master restarts communication) i) Wait until communication is re-established j) Check if a communication startup sequence has been triggered k) Check DS_UPLOAD_FLAG in parameter DataStorageIndex l) Read parameter DID (Device ID) and RID (Revision ID) and store to <idvalue_b> m) Check if an Event has been triggered (mode "Event appears") n) Read parameter DeviceStatus and store value to <status_b> o) Read selected parameter from a) and store values in array <parvalue_b>

Test parameter Only parameters in IODD are tested

- with attribute "accessRights = "rw", and

- which are not "excludedFromDataStorage",

and

- with attribute: defaultValue="<value>"

In case of the DeviceId is not equal to the DefaultDeviceId, the Test system needs the following information in addition

- the DefaultDeviceId to which the device is switched back

- the default parameter from the DefaultIODD

- The Testparameter must be taken from the DefaultIODD for comparison

Post condition – 1815

### Version V1.1.5 – 238 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Procedure g) shows communication interruption and procedure h) j) shows exactly one communication startup sequence

2. Values of <idvalue_b> match the default values of the IODD

RID: (CommNetworkProfile: iolinkRevision = "Vx.x")

3. Value of procedure k) (DS_UPLOAD_FLAG) = "0" (no DS_UPLOAD_REQ)

4. Procedure k) shows at least one Event of mode "Event appears" or value of

<status_b> = "0 (0x00)" For each read parameter (from procedure a)):

5. Response is positive

6. Values of <parvalue_b> match assigned "defaultValue"

Test passed All evaluations 1) to 6) are true

Test not passed At least one of the evaluations 1) to 6) is false

Report Communication from evaluation 1): <ok | nok> RevisionID/DeviceID from evaluation 2): <ok | nok> DataStorage from evaluation 3): <ok | nok> DeviceStatus from evaluation 4): <value>, <event> <ok | nok> For each received parameter (from evaluation 5) and 6)): Parameter: <Index>, <parvalue_a>, <parvalue_b> <ok | nok>

1816 1817

### Test Specification © IO-Link – 239 – Version V1.1.5

### 1818 8.4.7 IODD – Write alternative valid DeviceID

### 1819 Table 179 defines the test conditions for this test case.

### 1820 Table 179 – IODD – Write alternative valid DeviceID

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0314

Name TCD_DLPC_DID_OVERWRITE_COMP

Purpose (short) Device behavior with overwrite of the DeviceID (compatible)

Equipment under test (EUT) Device with support of an additional compatible DeviceID

Test case version 1.2

Category / type IODD functional system test: test to pass

Specification (clause) [1], 10.6.2, B.1.9

Configuration / setup Device-Tester-Unit 1821

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Device behavior with overwrite of the DeviceID with a compatible DeviceID as listed in the IODD. Master overwrites the DeviceID and the Device accepts the requested DeviceID. After a power off/on, Master checks if the previously written DeviceID has been stored in non-volatile memory in the Device.

Precondition DTU: SIO mode EUT: Device set to ("DID 0")

Procedure a) DTU establishes communication b) DTU reads DeviceID from Device (EUT) c) DTU overwrites the DeviceID with the DeviceID ("DID-2") by using the complete mechanism written in [19], Figure 76, including Restart d) Device (EUT) power switched off and on again e) DTU re-establishes communication f) DTU reads DeviceID from Device (EUT) g) DTU overwrites the DeviceID(“DID-2”) with “DID-0” to return to the previous settings for the next test by using the complete mechanism written in [19], Figure 76, including Restart

Test parameter IODD: DeviceIdentity/@deviceId, memorized as "DID-0" DeviceIdentity/@additionalDeviceIds, one of the DeviceIds memorized as "DID-2"

Post condition – 1822

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check acquired DeviceID after b)

2. Check acquired DeviceID after f)

Test passed DeviceID = “DID-0”in 1) DeviceID = "DID-2" in 2)

Test not passed (examples) Any of the checks failed.

Report Additional DeviceID stored in non-volatile memory: <yes/no> <ok | nok>

1823

1824

### Version V1.1.5 – 240 – Test Specification © IO-Link

### 1825 8.4.8 IODD – Write alternative invalid DeviceID

### 1826 Table 180 defines the test conditions for this test case.

### 1827 Table 180 – IODD – Write alternative invalid DeviceID

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0315

Name TCD_DLPC_DID_OVERRIDE_INCOMP

Purpose (short) Device behavior with overwrite of the DeviceID (incompatible)

Equipment under test (EUT) Device with or without support of an additional incompatible DeviceID

Test case version 1.2

Category / type IODD functional system test: test to fail

Specification (clause) [1], 10.6.2

Configuration / setup Device-Tester-Unit 1828

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Device behavior at overwrite of the DeviceID with incompatible value. Master overwrites the DeviceID and the Device rejects the requested DeviceID.

Precondition DTU: SIO mode EUT: Device set to ("DID-0")

Procedure a) DTU establishes communication b) DTU reads DeviceID from Device (EUT) c) DTU overwrites the DeviceID with any DeviceID (≠ "DID-0" or "DID-x") by using the complete mechanism written in [7], Figure 76, including Restart

d) DTU reads DeviceID from Device (EUT)

e) Device (EUT) power switched off and on again f) DTU re-establishes communication g) DTU reads DeviceID from Device (EUT)

Test parameter IODD: DeviceIdentity/@deviceId, memorized as "DID-0" DeviceIdentity@additionalDeviceIds = "DID-x", the additional DeviceIDs in the IODD

Post condition – 1829

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check acquired DeviceID after b)

2. Check acquired DeviceID after d)

3. Check acquired DeviceID after g)

Test passed DeviceID = “DID-0” in 1) DeviceID = "DID-0" in 2)

DeviceID = "DID-0" in 3)

Test not passed (examples) Any of the evaluation failed

Report Device don’t change to invalid DeviceID: <yes/no> <ok | nok>

1830 1831 1832 1833

### 1834 8.5 Test report summary of the IODD based Device tests

### 1835 The template is defined by the Device-Tester-System. The test report shall present at least the

### 1836 results of the test cases for the IODD based Device tests.

1837

### Test Specification © IO-Link – 241 – Version V1.1.5

### 1838 9 Master protocol tests

### 1839 9.1 General

### 1840 The protocol tests can be performed almost automatically with the help of a Master-Tester-

### 1841 System as defined in A.3. The test sequences are described in 5.5 together with a list of the

### 1842 relevant test cases for Master in Table 6. Supplementary requirements for Legacy-Masters be-

### 1843 yond the definitions in [5] are listed in B.

### 1844 Procedure results not mentioned in the evaluation part are assumed to be successful for pass-

### 1845 ing the tests.

### 1846 Master test cases are specified using appropriate types of macros as defined in Annex A.4.

### 1847 9.2 Timings

### 1848 9.2.1 Delay times after WURQ and Master messages (TDMT)

### 1849 Table 181 defines the test conditions for this test case.

### 1850 Table 181 – Delay times after WURQ and Master messages (TDMT)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0158

Name TCM_ PHYL_TIME_TDMT

Purpose (short) Check delay times after WURQ and Master messages

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], 7.3.2.2, Table 42

Configuration / setup Master-Tester-System 1851

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Measuring the delays between WURQ and first request (230,4 kbit/s) or between the requests in the individual transmission rates, respectively. Check if TDMT is within the tolerance of 27 to 37 TBIT of the subsequent transmission rate.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_Deactivate b) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) b) MTU_Timing_Startup;returning TDMT(COM3), TDMT(COM2), TDMT(COM1) c) Evaluation 1) d) Evaluation 2) e) Evaluation 3)

Test parameter –

Post condition – 1852

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check TDMT(COM3);See Figure 31 in [1], time from begin WURQ to begin start bit of COM3 minus TREN with 500 μs

2. Check TDMT(COM2);See Figure 31 in [1], time from begin stop bit COM3 to

begin start bit COM2 minus 1 TBIT(COM3)

3. Check TDMT(COM1);See Figure 31 in [1], time from begin stop bit COM2 to

begin start bit COM1 minus 1 TBIT(COM2)

Test passed In 1) 27 TBIT ≤ TDMT(COM3) ≤ 37 TBIT;TBIT = 4,34 μs In 2) 27 TBIT ≤ TDMT(COM2) ≤ 37 TBIT;TBIT = 26,04 μs In 3) 27 TBIT ≤ TDMT(COM1) ≤ 37 TBIT;TBIT = 208,33 μs

### Version V1.1.5 – 242 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

Test not passed (examples) Any TDMT out of tolerance

Report TDMT(COM3): <value> <ok | nok> TDMT(COM2): <value> <ok | nok> TDMT(COM1): <value> <ok | nok>

1853

### Test Specification © IO-Link – 243 – Version V1.1.5

### 1854 9.2.2 Delay time between three WURQs (TDWU)

### 1855 Table 182 defines the test conditions for this test case.

### 1856 Table 182 – Delay time between three WURQs (TDWU)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0159

Name TCM_ PHYL_TIME_TDWU

Purpose (short) Check whether delay time between wake-up retries is within tolerance

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], 7.3.2.2, Table 42

Configuration / setup Master-Tester-System 1857

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Measuring the times between the three Wake-up retries. Master-Tester-Unit shall detect the beginning of all Wake-up requests and measure the time in between. It shall not react to requests.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_Deactivate a) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) b) MTU_Timing_Startup;returning TDWU12 and TDWU23 c) Evaluation 1) d) Evaluation 2)

Test parameter –

Post condition – 1858

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check interval between first and second Wake-up: TDWU12

2. Check interval between second and third Wake-up: TDWU23

Test passed 30 ms ≤ TDWU12 ≤ 50 ms, and 30 ms ≤ TDWU23 ≤ 50 ms

Test not passed (examples) TDWU12 or TDWU23 out of tolerance

Report TDWU12 (interval between first and second): <value> <ok | nok> TDWU23 (interval between second and third): <value> <ok | nok>

1859

1860

### Version V1.1.5 – 244 – Test Specification © IO-Link

### 1861 9.2.3 Number of WURQs

### 1862 Table 183 defines the test conditions for this test case.

### 1863 Table 183 – Number of WURQs

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0160

Name TCM_ PHYL_TIME_NUMOFWURQS

Purpose (short) Check number of Wake-up retries

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], 7.3.2.2, Table 42

Configuration / setup Master-Tester-System 1864

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The number of WURQs during one Wake-up sequence is counted. The number of retries shall be exactly 2 (total number 3). Master-Tester-Unit shall detect the start of the first WURQ and then start time measurement. Another 2 WURQs shall follow within the next 100 ms. No further requests except these 3 WURQs shall follow within 500 ms after start of the first WURQ.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_Deactivate a) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) b) MTU_Timing_Startup;returning WURQ and WURQ101-500 counts c) Evaluation 1) d) Evaluation 2)

### Test parameter –

### Post condition –

1865

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check number of WURQs within first 100 ms: WURQ

2. Check number of WURQs within 101 ms to 500 ms: WURQ101-500

Test passed WURQ = 3, and WURQ101-500 = 0

Test not passed (examples) WURQ ≠ 3, or WURQ101-500 > 0

Report WURQ: <value> <ok | nok> WURQ101-500: <value> <ok | nok>

1866

1867

### Test Specification © IO-Link – 245 – Version V1.1.5

### 1868 9.2.4 Delay time between WURQ retry sequences (TSD)

### 1869 Table 184 defines the test conditions for this test case.

### 1870 Table 184 – Delay time between WURQ retry sequences (TSD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0161

Name TCM_ PHYL_TIME_TSD

Purpose (short) Check time between two WURQs is between 0,5 s and 1 s.

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], 7.3.2.2, Table 42

Configuration / setup Master-Tester-System 1871

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The time between start of two Wake-up sequences is measured. Master-Tester-Unit shall detect start of the first WURQ and start time measurement. Two more WURQs shall follow within the next 100 ms. No further requests except these 3 WURQs shall follow within 500 ms after start of the first WURQ. At the latest 1 s after the first WURQ, the Master shall start a new Wake-up sequence. Master-Tester-Unit shall check these times.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_Deactivate a) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) b) MTU_Timing_Startup;returning TSD c) Evaluation 1)

Test parameter –

Post condition – 1872

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check TSD

Test passed 0,5 s ≤ TSD ≤ 1 s

Test not passed (examples) TSD > 1 s

Report TSD: <value> <ok | nok>

1873

1874

### Version V1.1.5 – 246 – Test Specification © IO-Link

### 1875 9.2.5 Delay time between two Master messages at STARTUP (TINITCYC)

### 1876 Table 185 defines the test conditions for this test case.

### 1877 Table 185 – Delay time between two Master messages at STARTUP (TINITCYC)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0162

Name TCM_ PHYL_TIME_TINITCYC

Purpose (short) STARTUP: The time between two message beginnings shall be ≥ 100 TBIT.

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], 7.3.3.3, A.2.6, Table A.7

Configuration / setup Master-Tester-System 1878

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) In state STARTUP, the time between two Master message beginnings is measured. This is repeated for all transmission rates supported by the Master. Master-Tester- Unit detects the start of each Master message and measures the time in between. These times shall be ≥ 100 TBIT.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "com" (Test parameter) b) MTU_Timing_SetCommunicationMode = "com" c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) d) TM_AWAIT_PORT_STATUS(OPERATE) e) MTU_Timing_GetTinitcycInStartup;returning TINITCYC f) Evaluation 1) g) SMI_PortConfiguration(ABPS_PORTINACTIVE) h) Repeat from b) with next "com"

Test parameter com = {COM1, COM2, COM3}

Post condition – 1879

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check TINITCYC

Test passed Minimum of TINITCYC (COM1) ≥ 100 TBIT, and Minimum of TINITCYC (COM2) ≥ 100 TBIT, and Minimum of TINITCYC (COM3) ≥ 100 TBIT

Test not passed (examples) Any of the minima of TINITCYC is < 100 TBIT

Report Minimum of TINITCYC (COM1): <value> Minimum of TINITCYC (COM2): <value> Minimum of TINITCYC (COM3): <value> <ok | nok>

1880

1881

### Test Specification © IO-Link – 247 – Version V1.1.5

### 1882 9.2.6 Delay time of Master messages with 2 octets OD in PREOP (TINITCYC)

### 1883 Table 186 defines the test conditions for this test case.

### 1884 Table 186 – Delay time of Master messages with 2 octets OD in PREOP (TINITCYC)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0331

Name TCM_ PHYL_TIME_TINITCYC_PREOP_2

Purpose (short) PREOPERATE: TINITCYC of messages with 2 octets OD shall be ≥ 100 TBIT

Equipment under test (EUT) Master

Test case version 1.0

Category / type Master protocol test: test to pass

Specification (clause) [1], 7.3.3.3, A.2.6, Table A.8

Configuration / setup Master-Tester-System 1885

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) In state PREOPERATE, the time between the start of two Master messages with 2 octets On-request Data is measured. This is repeated for all transmission rates supported by the Master. Master-Tester-Unit detects the start of each Master message and measures the time in between. These times shall be ≥ 100 TBIT.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE;2 octets OD

Procedure a) Assign first value to "com" (Test parameter) b) MTU_Timing_SetCommunicationMode = "com" c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) d) TM_AWAIT_PORT_STATUS(OPERATE) e) MTU_Timing_GetTinitcycInPreoperate;returning TINITCYC f) Evaluation 1) g) SMI_PortConfiguration(ABPS_PORTINACTIVE) h) Repeat from b) with next "com"

Test parameter com = {COM1, COM2, COM3}

Post condition – 1886

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check TINITCYC

Test passed Minimum of TINITCYC (COM1) ≥ 100 TBIT, and Minimum of TINITCYC (COM2) ≥ 100 TBIT, and Minimum of TINITCYC (COM3) ≥ 100 TBIT

Test not passed (examples) Any of the minima of TINITCYC is at least < 100 TBIT

Report Minimum of TINITCYC (COM1): <value> Minimum of TINITCYC (COM2): <value> Minimum of TINITCYC (COM3): <value> <ok | nok>

1887

1888

### Version V1.1.5 – 248 – Test Specification © IO-Link

### 1889 9.2.7 Delay time of Master messages with 8 octets OD in PREOP (TINITCYC)

### 1890 Table 187 defines the test conditions for this test case.

### 1891 Table 187 – Delay time of Master messages with 8 octets OD in PREOP (TINITCYC)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0332

Name TCM_ PHYL_TIME_TINITCYC_PREOP_8

Purpose (short) PREOPERATE: TINITCYC of messages with 8 octets OD shall be ≥ 210 TBIT

Equipment under test (EUT) Master

Test case version 1.0

Category / type Master protocol test: test to pass

Specification (clause) [1], 7.3.3.3, A.2.6, Table A.8

Configuration / setup Master-Tester-System 1892

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) In state PREOPERATE, the time between the start of two Master messages with 8 octets On-request Data is measured. This is repeated for all transmission rates supported by the Master. Master-Tester-Unit detects the start of each Master message and measures the time in between. These times shall be ≥ 210 TBIT.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "com" (Test parameter) b) MTU_Timing_SetCommunicationMode = "com" c) MTU_DPP1_Set(M-sequenceCapability) = 0x21;8 octets On-request Data d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) e) TM_AWAIT_PORT_STATUS(OPERATE) f) MTU_Timing_GetTinitcycInPreoperate;returning TINITCYC g) Evaluation 1) h) SMI_PortConfiguration(ABPS_PORTINACTIVE) i) Repeat from b) with next "com"

Test parameter com = {COM1, COM2, COM3}

Post condition – 1893

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check TINITCYC

Test passed Minimum of TINITCYC (COM1) ≥ 210 TBIT, and Minimum of TINITCYC (COM2) ≥ 210 TBIT, and Minimum of TINITCYC (COM3) ≥ 210 TBIT

Test not passed (examples) Any of the minima of TINITCYC is at least < 210 TBIT

Report Minimum of TINITCYC (COM1): <value> Minimum of TINITCYC (COM2): <value> Minimum of TINITCYC (COM3): <value> <ok | nok>

1894

1895

### Test Specification © IO-Link – 249 – Version V1.1.5

### 1896 9.2.8 Delay time of Master messages with 32 octets OD in PREOP (TINITCYC)

### 1897 Table 188 defines the test conditions for this test case.

### 1898 Table 188 – Delay time of Master messages with 32 octets OD in PREOP (TINITCYC)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0333

Name TCM_ PHYL_TIME_TINITCYC_PREOP_32

Purpose (short) PREOPERATE: TINITCYC of messages with 32 octets OD shall be ≥ 550 TBIT

Equipment under test (EUT) Master

Test case version 1.0

Category / type Master protocol test: test to pass

Specification (clause) [1], 7.3.3.3, A.2.6, Table A.8

Configuration / setup Master-Tester-System 1899

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) In state PREOPERATE, the time between the start of two Master messages with 32 octets On-request Data is measured. This is repeated for all transmission rates supported by the Master. Master-Tester-Unit detects the start of each Master message and measures the time in between. These times shall be ≥ 550 TBIT.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "com" (Test parameter) b) MTU_Timing_SetCommunicationMode = "com" c) MTU_DPP1_Set(M-sequenceCapability) = 0x31;32 octets On-request Data d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) e) TM_AWAIT_PORT_STATUS(OPERATE) f) MTU_Timing_GetTinitcycInPreoperate;returning TINITCYC g) Evaluation 1) h) SMI_PortConfiguration(ABPS_PORTINACTIVE) i) Repeat from b) with next "com"

Test parameter com = {COM1, COM2, COM3}

Post condition – 1900

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check TINITCYC

Test passed Minimum of TINITCYC (COM1) ≥ 550 TBIT, and Minimum of TINITCYC (COM2) ≥ 550 TBIT, and Minimum of TINITCYC (COM3) ≥ 550 TBIT

Test not passed (examples) Any of the minima of TINITCYC is at least < 550 TBIT

Report Minimum of TINITCYC (COM1): <value> Minimum of TINITCYC (COM2): <value> Minimum of TINITCYC (COM3): <value> <ok | nok>

1901

1902

### Version V1.1.5 – 250 – Test Specification © IO-Link

### 1903 9.2.9 Adjustment of the MasterCycleTime

### 1904 Table 189 defines the test conditions for this test case.

### 1905 Table 189 – Adjustment of the MasterCycleTime

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0163

Name TCM_ PHYL_TIME_MASTERCYCLETIME

Purpose (short) The Master shall adapt correctly to a too short MinCycleTime of the Device

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], B.1.3

Configuration / setup Master-Tester-System 1906

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master-Tester-Unit shall suggest the M-sequence type for a too small MinCycleTime value. The Master shall not accept this time and shall write back a feasible Master- Cycle time. The same applies if the MinCycleTime value is "0". At each M-sequence type, MTU starts with the MinCycleTime value "0" and with times below the time the Master can achieve. The Master shall correct these times by writing back a possible MasterCycleTime value.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "com", "minCT" (Test parameter) b) MTU_Timing_SetCommunicationMode = "com";"Device" uses COMx c) MTU_DPP1_Set(MinCycleTime) = "minCT";"Device" uses too small minCT d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);Master corrects value e) TM_AWAIT_PORT_STATUS(OPERATE) f) MTU_DPP1_Get(MasterCycleTime);returning mstCT g) Evaluation 1) h) SMI_PortConfiguration(ABPS_PORTINACTIVE) i) Repeat from b) with next "com", "minCT"

Test parameter com = {COM1, COM1, COM2, COM2, COM3, COM3} minCT = {0x00, 0x52 (13,6 ms), 0x00, 0x11 (1,7 ms), 0x00, 0x03 (0,3 ms)}

Post condition – 1907

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check all mstCT

Test passed COM1, minCT = 0x00: mstCT ≥ 0x5B (17,2 ms) COM1, minCT = 0x52: mstCT ≥ 0x53 (14,0 ms) COM2, minCT = 0x00: mstCT ≥ 0x16 (2,2 ms) COM2, minCT = 0x11: mstCT ≥ 0x12 (1,8 ms) COM3, minCT = 0x00: mstCT ≥ 0x04 (0,4 ms) COM3, minCT = 0x03: mstCT ≥ 0x04 (0,4 ms)

Test not passed (examples) Any of the mstCT values < Master limits (see Test passed)

Report mstCT (COM1, minCT = 0x00): <value> <ok | nok> mstCT (COM1, minCT = 0x52): <value> <ok | nok> mstCT (COM2, minCT = 0x00): <value> <ok | nok> mstCT (COM2, minCT = 0x11): <value> <ok | nok> mstCT (COM3, minCT = 0x00): <value> <ok | nok> mstCT (COM3, minCT = 0x03): <value> <ok | nok>

1908

1909

### Test Specification © IO-Link – 251 – Version V1.1.5

### 1910 9.2.10 Written MasterCycleTime corresponds to real cycle time

### 1911 Table 190 defines the test conditions for this test case. It should be noted that communication

### 1912 load at other Ports can interfere with the performance of this test.

### 1913 Table 190 – Written MasterCycleTime corresponds to real cycle time

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0164

Name TCM_ PHYL_TIME_MASTERCYCLETIMEREAL

Purpose (short) Written MasterCycleTime matches real cycle time

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], 7.3.3.3, A.2.6, Table A.11

Configuration / setup Master-Tester-System 1914

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Master shall read the MinCycleTime value from Master-Tester-Unit and shall write back its MasterCycleTime value. This time shall be checked by the Master- Tester-Unit. To do so, Master-Tester-Unit shall start several times with different MinCycleTimes and then check them. Master-Tester-Unit receives different values in the Direct Parameter page 1 for the MinCycleTime and carries out a start-up to state OPERATE. In this state the MasterCycleTime is checked.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "com", "minCT" b) MTU_Timing_SetCommunicationMode = "com;"Device" uses COMx c) MTU_DPP1_Set(MinCycleTime) = "minCT";"Device" uses too small minCT d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);Master corrects value e) TM_AWAIT_PORT_STATUS(OPERATE) f) MTU_DPP1_Get(MasterCycleTime);returning Texp g) MTU_Timing_GetTcycInOperate;returning TCYC h) Evaluation 1) j) SMI_PortConfiguration(ABPS_PORTINACTIVE) k) Repeat from b) with next "com", "minCT"

Test parameter com = {COM1, COM1, COM1, COM1, COM1, COM2, COM2, COM2, COM2, COM2, COM2, COM2, COM3, COM3, COM3, COM3, COM3, COM3, COM3 } minCT = {0x00, 0x5D, 0x7F, 0x80, 0xBF, 0x00, 0x17, 0x3F, 0x40, 0x7F, 0x80, 0xBF, 0x00, 0x04, 0x3F, 0x40, 0x7F, 0x80, 0xBF};see Table A.11 in [1] for recommended MinCycleTimes and Table B.3 in [1] for crossover values

Post condition – 1915

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check TCYC and Texp

Test passed Minimum of TCYC ≥ Texp - 1% Maximum of TCYC ≤ Texp +10%

Test not passed (examples) Minimum of TCYC < Texp - 1% Maximum of TCYC > Texp +10%

Report Minimum of TCYC: <value> <ok | nok> Maximum of TCYC: <value> <ok | nok>

1916

1917

### Version V1.1.5 – 252 – Test Specification © IO-Link

### 1918 9.2.11 Master tolerates different Device response times

### 1919 Table 191 defines the test conditions for this test case.

### 1920 Table 191 – Master tolerates different Device response times

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0165

Name TCM_ PHYL_TIME_DEVRESPTIMES

Purpose (short) Master tolerates different Device response times

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master protocol test: test to pass

Specification (clause) [1], A.3.5

Configuration / setup Master-Tester-System 1921

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master-Tester-Unit shall answer with different response times after receiving the Master message for all transmission rates COM1, COM2, and COM3. The Master shall be able to handle deviations (jitter). Master-Tester-Unit responds with different response times between 1 and 10 TBIT.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "com" (Test parameter) b) MTU_Timing_SetCommunicationMode = "com" c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) d) TM_AWAIT_PORT_STATUS(OPERATE) e) Assign first value to "TA" (Test parameter) f) MTU_Timing_SetReplyMessageDelay(TA) = "TA" g) MTU_State_GetMasterRetryCTviolCount;clear RETRIES, CTVIOLS h) SMI_Device Read(ABPS_DEVICEREAD(Index=16));Vendor-Name i) MTU_State_GetMasterRetryCTviolCount;returning RETRIES, CTVIOLS j) Evaluation 1) k) Repeat from f) with next "TA" l) SMI_PortConfiguration(ABPS_PORTINACTIVE) m) Repeat from b) with next "com"

Test parameter com = {COM1, COM2, COM3} TA = {1 TBIT, 5 TBIT, 10 TBIT};1 TBIT or as fast as possible

Post condition – 1922

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check RETRIES, CTVIOLS

Test passed RETRIES = 0 and CTVIOLS = 0

Test not passed (examples) RETRIES > 0 or CTVIOLS > 0

Report CTVIOLS: <value> <ok | nok> RETRIES: <value> <ok | nok>

1923

1924

### Test Specification © IO-Link – 253 – Version V1.1.5

### 1925 9.2.12 Master tolerates different UART frame delay times (T2)

### 1926 Table 192 defines the test conditions for this test case.

### 1927 Table 192 – Master tolerates different UART frame delay times (T2)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0166

Name TCM_ PHYL_TIME_UARTT2

Purpose (short) Master tolerates different UART frame delay times (T2) of the Device.

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master protocol test: test to pass

Specification (clause) [1], A.3.4

Configuration / setup Master-Tester-System 1928

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master-Tester-Unit shall respond with different delays between the octets at all transmission rates COM1, COM2, and COM3. The Master shall be able to handle this variation. Master-Tester-Unit responds with different delays between 0 and 3 TBIT.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "com" (Test parameter) b) MTU_Timing_SetCommunicationMode = "com" c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) d) TM_AWAIT_PORT_STATUS(OPERATE) e) Assign first value to "T2" (Test parameter) f) MTU_Timing_SetReplyMessageDelay(T2) = "T2" g) MTU_State_GetMasterRetryCTviolCount;clear RETRIES, CTVIOLS h) SMI_Device Read(ABPS_DEVICEREAD(Index=16));Vendor-Name i) MTU_State_GetMasterRetryCTviolCount;returning RETRIES, CTVIOLS j) Evaluation 1) k) Repeat from f) with next "T2" l) SMI_PortConfiguration(ABPS_PORTINACTIVE) m) Repeat from b) with next "com"

Test parameter com = {COM1, COM2, COM3} T2 = {0 TBIT, 1 TBIT, 2 TBIT, 3 TBIT}

Post condition – 1929

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check RETRIES, CTVIOLS

Test passed RETRIES = 0 and CTVIOLS = 0

Test not passed (examples) RETRIES > 0 or CTVIOLS > 0

Report CTVIOLS: <value> <ok | nok> RETRIES: <value> <ok | nok>

1930

1931

### Version V1.1.5 – 254 – Test Specification © IO-Link

### 1932 9.2.13 Master sends UART frames within tolerated times (T1)

### 1933 Table 193 defines the test conditions for this test case.

### 1934 Table 193 – Master sends UART frames within tolerated times (T1)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0167

Name TCM_ PHYL_TIME_UARTT1

Purpose (short) Master sends UART frames within tolerated times (T1)

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master protocol test: test to pass

Specification (clause) [1], A.3.3

Configuration / setup Master-Tester-Unit 1935

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Measuring the delays between the UART octets for all transmission rates COM1, COM2, and COM3. Master-Tester-Unit shall measure the delays between the end of the stop bit and the beginning of the start bit of the next octet.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "com" (Test parameter) b) MTU_Timing_SetCommunicationMode = "com" c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) d) TM_AWAIT_PORT_STATUS(OPERATE) e) MTU_Timing_Get_T1;returning T1 f) Evaluation 1) g) SMI_PortConfiguration(ABPS_PORTINACTIVE) h) Repeat from b) with next "com"

Test parameter com = {COM1, COM2, COM3}

Post condition – 1936

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check T1

Test passed Maximum of T1 ≤ 1 TBIT

Test not passed (examples) Maximum of T1 > 1 TBIT

Report Maximum of T1: <value> <ok | nok>

1937

1938

### Test Specification © IO-Link – 255 – Version V1.1.5

### 1939 9.3 Process Data (PD)

### 1940 9.3.1 TYPE_2_1 for 8 bit PD input

### 1941 Table 194 defines the test conditions for this test case.

### 1942 Table 194 – TYPE_2_1 for 8 bit PD input

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0168

Name TCM_ DLPD_CYCC_TYPE21BIT8IN

Purpose (short) Master uses M-sequence TYPE_2_1 for 8 bit Process Data input

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

### Specification (clause) [1], 9.2.3.5,11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (row 5)

Configuration / setup Master-Tester-System 1943

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master reads PD input length = 8 bit and output length = 0 bit during STARTUP from Device. Master shall select M-sequence TYPE_2_1 in OPERATE and propagate this information to SMI_PDin, SMI_PDInOut and SMI_PortStatus services.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

### Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x11;see B.1.4 in [1]

b) MTU_DPP1_Set(PDIn length) = 0x08;"8" Bit c) MTU_DPP1_Set(PDOut length) = 0x00;"0" Bit d) MTU_PD_Set = 0x12;PDIn = 0x12 e) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);Switch to OPERATE f) TM_AWAIT_PORT_STATUS(OPERATE);Wait for OPERATE g) MTU_State_CheckOperate;returns "MTU in OPERATE" h) Evaluation 1) i) SMI_PDIn;returns "ArgBlock PDIn" j) Evaluation 2) k) SMI_PDInOut;returns "ArgBlock PDInOut" l) Evaluation 3) m) SMI_PortStatus;returns "ArgBlock PortStatusList" n) Evaluation 4) o) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>) p) Evaluation 5)

Test parameter –

Post condition – 1944

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "ArgBlock PDIn"

3. Check "ArgBlock PDInOut"

4. Check "ArgBlock PortStatusList"

5. Check <vendorname>

Test passed "MTU in OPERATE" = TRUE, and PDIn: PQI = 0xA0, InputDataLength = 1, PDI0 = [0x12], and PDInOut: PQI = 0xA0, OE = 0x00, InputDataLength = 1, PDI0 = [0x12], OutputDataLength = 0, and PortStatusList: PortQualityInfo = 0x02, RevisionID = 0x11, InputDataLength = 1, OutputDataLength = 0, and <vendorname> = IO-Link Community

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report <vendorname> correct: <yes/no> <ok | nok>

1945

### Version V1.1.5 – 256 – Test Specification © IO-Link

### 1946 9.3.2 TYPE_2_2 for 16 bit PD input

### 1947 Table 195 defines the test conditions for this test case.

### 1948 Table 195 – TYPE_2_2 for 16 bit PD input

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0169

Name TCM_ DLPD_CYCC_TYPE22BIT16IN

Purpose (short) Master uses M-sequence TYPE_2_2 for 16 bit Process Data input

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master M-sequence test: test to pass

### Specification (clause) [1], 9.2.3.5,11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (row 6)

Configuration / setup Master-Tester-System 1949

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master reads PD input length = 16 bit and output length = 0 bit during STARTUP from Device. Master shall select M-sequence TYPE_2_2 in OPERATE and propagate this information to SMI_PDin, SMI_PDInOut and SMI_PortStatus services.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

### Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x11;see B.1.4 in [1]

b) MTU_DPP1_Set(PDIn length) = 0x10;"16" Bit c) MTU_DPP1_Set(PDOut length) = 0x00;"0" Bit d) MTU_PD_Set = 0x1234;PDIn = 0x1234 e) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);Switch to OPERATE f) TM_AWAIT_PORT_STATUS (OPERATE);Wait for OPERATE g) MTU_State_CheckOperate;returns "MTU in OPERATE" h) Evaluation 1) i) SMI_PDIn;returns "ArgBlock PDIn" j) Evaluation 2) k) SMI_PDInOut;returns "ArgBlock PDInOut" l) Evaluation 3) m) SMI_PortStatus;returns "ArgBlock PortStatusList" n) Evaluation 4) o) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>) p) Evaluation 5)

Test parameter –

Post condition – 1950

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "ArgBlock PDIn"

3. Check "ArgBlock PDInOut"

4. Check "ArgBlock PortStatusList"

5. Check <vendorname>

Test passed "MTU in OPERATE" = TRUE, and PDIn: PQI = 0xA0, InputDataLength = 2, PDI = [0x12, 0x34], and PDInOut: PQI = 0xA0, OE = 0x00, InputDataLength = 2, PDI = [0x12, 0x34], OutputDataLength = 0, and PortStatusList: PortQualityInfo= 0x02, RevisionID = 0x11, InputDataLength = 2, OutputDataLength = 0, and <vendorname> = "IO-Link Community"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report <vendorname> correct: <yes/no> <ok | nok>

1951

### Test Specification © IO-Link – 257 – Version V1.1.5

### 1952 9.3.3 TYPE_2_3 for 8 bit PD output

### 1953 Table 196 defines the test conditions for this test case.

### 1954 Table 196 – TYPE_2_3 for 8 bit PD output

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0170

Name TCM_ DLPD_CYCC_TYPE23BIT8OUT

Purpose (short) Master uses M-sequence TYPE_2_3 for 8 bit Process Data output

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5,11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (row 7)

Configuration / setup Master-Tester-System 1955

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master reads PD input length = 0 bit and output length = 8 bit during STARTUP from Device. Master shall select M-sequence TYPE_2_3 in OPERATE and propagate this information to SMI_PDIn, SMI_PDInOut and SMI_PortStatus services.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x11;see B.1.4 in [1] b) MTU_DPP1_Set(PDIn length) = 0x00;”0” Bit c) MTU_DPP1_Set(PDOut length) = 0x08;”8” Bit d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);Switch to OPERATE e) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE state f) MTU_State_CheckOperate;returns "MTU in OPERATE" g) Evaluation 1) h) SMI_PDOut(ABPS_PDOUT<PDO0=0x12>);PDO0 = 0x12 i) SMI_PDInOut;returns "ArgBlock PDInOut" j) Evaluation 2) k) MTU_PD_Get;returns "PDOut values" l) Evaluation 3) m) SMI_PortStatus;returns "ArgBlock PortStatusList" n) Evaluation 4) o) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>) p) Evaluation 5)

Test parameter –

Post condition – 1956

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "ArgBlock PDInOut"

3. Check "PDout values"

4. Check "ArgBlock PortStatusList"

5. Check <vendorname>

Test passed "MTU in OPERATE" = TRUE, and PDInOut: PQI = 0xA0, OE = 1, InputDataLength = 0, OutputDataLength = 1, PDO0 = [0x12], and "PDout values" = 0x12, and PortStatusList: PortQualityInfo= 0x00, RevisionID = 0x11, InputDataLength = 0, OutputDataLength = 1, and <vendorname> = "IO-Link Community"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report <vendorname> correct: <yes/no> <ok | nok>

1957

### Version V1.1.5 – 258 – Test Specification © IO-Link

### 1958 9.3.4 TYPE_2_4 for 16 bit PD output

### 1959 Table 197 defines the test conditions for this test case.

### 1960 Table 197 – TYPE_2_4 for 16 bit PD output

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0171

Name TCM_ DLPD_CYCC_TYPE24BIT16OUT

Purpose (short) Master uses M-sequence TYPE_2_4 for 16 bit Process Data output

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5,11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (row 8)

Configuration / setup Master-Tester-System 1961

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master reads PD input length = 0 bits, Output length = 16 bit during STARTUP from Device. Master shall select M-sequence TYPE_2_4 in OPERATE and propagate this information to SMI_PDIn, SMI_PDInOut and SMI_PortStatus services.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x11;see B.1.4 in [1] b) MTU_DPP1_Set(PDIn length) = 0x00;”0” Bit c) MTU_DPP1_Set(PDOut length) = 0x10;”16” Bit d) SMI_PortConfiguration (ABPS_NOTYPE_CHECK);switch to OPERATE e) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE f) MTU_State_CheckOperate;returns "MTU in OPERATE" g) Evaluation 1) h) SMI_PDOut(ABPS_PDOUT<PDO0=0x12, PDO1=0x34>);set PDOut = 0x1234 i) SMI_PDInOut;returns "ArgBlock PDInOut" j) Evaluation 2) k) MTU_PD_Get;returns "PDOut values" l) Evaluation 3) m) SMI_PortStatus;returns "ArgBlock PortStatusList" n) Evaluation 4) o) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>)

Test parameter –

Post condition – 1962

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "ArgBlock PDInOut"

3. Check "PDout values"

4. Check "ArgBlock PortStatusList"

5. Check <vendorname>

Test passed "MTU in OPERATE" = TRUE, and PDInOut: PQI = 0xA0, OE = 1, InputDataLength = 0, OutputDataLength = 2, PDO = [0x12, 0x34], and "PDout values" = 0x12, 0x34, and PortStatusList: PortQualityInfo= 0x00, RevisionID = 0x11, InputDataLength = 0, OutputDataLength = 2, and <vendorname> = "IO-Link Community"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report <vendorname> correct: <yes/no> <ok | nok>

1963

### Test Specification © IO-Link – 259 – Version V1.1.5

### 1964 9.3.5 TYPE_2_5 for 8/8 bit PD in/output

### 1965 Table 198 defines the test conditions for this test case.

### 1966 Table 198 – TYPE_2_5 for 8/8 bit PD in/output

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0172

Name TCM_ DLPD_CYCC_TYPE25BIT8INBIT8OUT

Purpose (short) Master uses M-sequence TYPE_2_5 for 8/8 bit Process Data in/output

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5,11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (row 9)

Configuration / setup Master-Tester-System 1967

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master reads PD input length = 8 bits, Output length = 8 bit during STARTUP from Device. Master shall select M-sequence TYPE_2_5 in OPERATE and propagate this information to the SMI_PDIn, SMI_PDInOut and SMI_PortStatus services.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x11;see B.1.4 in [1] b) MTU_DPP1_Set(PDIn length) = 0x08;”8” Bit c) MTU_DPP1_Set(PDOut length) = 0x08;”8” Bit d) MTU_PD_Set = 0x12;PDIn = 0x12 e) SMI_PortConfiguration (ABPS_NOTYPE_CHECK);Switch to OPERATE f) TM_AWAIT_PORT_STATUS(OPERATE);Wait for OPERATE g) MTU_State_CheckOperate;returns "MTU in OPERATE" h) Evaluation 1) i) SMI_PDOut(ABPS_PDOUT<PDO0=0x34>);PDO0 = 0x34 j) SMI_PDIn;returns "ArgBlock PDIn" k) Evaluation 2) l) SMI_PDInOut;returns "ArgBlock PDInOut" m) Evaluation 3) n) MTU_PD_Get;returns "PDOut values" o) Evaluation 4) p) SMI_PortStatus;returns "ArgBlock PortStatusList" q) Evaluation 5) r) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>) s) Evaluation 6)

Test parameter –

Post condition – 1968

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "ArgBlock PDIn"

3. Check "ArgBlock PDInOut"

4. Check "PDout values"

5. Check "ArgBlock PortStatusList"

6. Check <vendorname>

Test passed "MTU in OPERATE" = TRUE, and PDIn: PQI = 0xA0, InputDataLength = 1, PDI0 = [0x12], and PDInOut: PQI = 0xA0, OE = 1, InputDataLength = 1, PDI0 = [0x12], OutputDataLength = 1, PDO0 = [0x34], and "PDout values" = 0x34, and PortStatusList: PortQualityInfo=0x00, RevisionID = 0x11, InputDataLength = 1, OutputDataLength = 1, and <vendorname> = "IO-Link Community"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report <vendorname> correct: <yes/no> <ok | nok>

1969

### Version V1.1.5 – 260 – Test Specification © IO-Link

### 1970 9.3.6 TYPE_2_V for 16/16 bit PD in/output and 1 octet OD

### 1971 Table 198 defines the test conditions for this test case.

### 1972 Table 199 – TYPE_2_ V for 16/16 bit PD in/output and 1 octet OD

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0298

Name TCM_ DLPD_CYCC_TYPE2VBIT16INBIT16OUT

Purpose (short) Master uses M-sequence TYPE_2_V for 16/16 bit Process Data in/output

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5,11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (row 10 and 11)

Configuration / setup Master-Tester-System 1973

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master reads PD input length = 16 bit, Output length = 16 bit during STARTUP from Device. Master shall select M-sequence TYPE_2_V with 1 octet OD in OPERATE and propagate information to SMI_PDIn, SMI_PDInOut, and SMI_PortStatus.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x11;see B.1.4 in [1] b) MTU_DPP1_Set(PDIn length) = 0x10;”16” Bit c) MTU_DPP1_Set(PDOut length) = 0x10;”16” Bit d) MTU_PD_Set = 0x1234;PDIn = 0x1234 e) SMI_PortConfiguration (ABPS_NOTYPE_CHECK);switch to OPERATE f) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE g) MTU_State_CheckOperate;returns "MTU in OPERATE" h) Evaluation 1) i) SMI_PDOut(ABPS_PDOUT<PDO0=0x56, PDO1=0x78>);PDO = 0x5678 j) SMI_PDIn;returns "ArgBlock PDIn" k) Evaluation 2) l) SMI_PDInOut;returns "ArgBlock PDInOut" m) Evaluation 3) n) MTU_PD_Get;returns "PDOut values" o) Evaluation 4) p) SMI_PortStatus;returns "ArgBlock PortStatusList" q) Evaluation 5) r) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>) s) Evaluation 6)

Test parameter –

Post condition – 1974

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "ArgBlock PDIn"

3. Check "ArgBlock PDInOut"

4. Check "PDout values"

5. Check "ArgBlock PortStatusList"

6. Check <vendorname>

Test passed "MTU in OPERATE" = TRUE, and PDIn: PQI = 0xA0, InputDataLength = 2, PDI = [0x12, 0x34], and PDInOut: PQI = 0xA0, OE = 0x01, InputDataLength = 2, PDI = [0x12, 0x34], OutputDataLength = 2, PDO = [0x56, 0x78], and "PDout values" = 0x56, 0x78, and PortStatusList: PortQInfo = 0x00, RevID = 0x11, InDLen = 2, OutDLen = 2, and <vendorname> = "IO-Link Community"

Test not passed (examples) Any of the checks in Test passed failed or communication error

### Test Specification © IO-Link – 261 – Version V1.1.5

TEST CASE RESULTS CHECK / REACTION

Report <vendorname> correct: <yes/no> <ok | nok>

1975

### Version V1.1.5 – 262 – Test Specification © IO-Link

### 1976 9.3.7 TYPE_1_1 for 32 octets PD in and 2 octets OD

### 1977 Table 200 defines the test conditions for this test case.

### 1978 Table 200 – TYPE_1_1 for 32 octets PD in and 2 octets OD

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0173

Name TCM_ DLPD_CYCC_TYPE1OCTET32IN

Purpose (short) Master uses M-sequence TYPE_1_1 for 32/0 octets PD input and 2 octets OD

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5,11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (row 3)

Configuration / setup Master-Tester-System 1979

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master reads PD input length = 256 bits, Output length = 0 bit during STARTUP from Device. Master shall select M-sequence TYPE_1_1 with 2 octets OD in OPERATE and propagate this information to SMI_PDIn, SMI_PDInOut and SMI_PortStatus services.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x13;see B.1.4 in [1] b) MTU_DPP1_Set(RevisionID) = 0x10;see B.1.1 in [1] c) MTU_DPP1_Set(PDIn length) = 0x100;”256” Bit = 32 octets d) MTU_DPP1_Set(PDOut length) = 0x00;”0” Bit e) MTU_PD_Set = 2^256-1;all Bits “1” f) SMI_PortConfiguration (ABPS_NOTYPE_CHECK);switch to OPERATE g) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE h) MTU_State_CheckOperate;returns "MTU in OPERATE" i) Evaluation 1) j) SMI_PDIn;returns "ArgBlock PDIn" k) Evaluation 2) l) SMI_PDInOut;returns "ArgBlock PDInOut" m) Evaluation 3) n) SMI_PortStatus;returns "ArgBlock PortStatusList" o) Evaluation 4) p) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>) q) Evaluation 5)

Test parameter –

Post condition – 1980

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "ArgBlock PDIn"

3. Check "ArgBlock PDInOut"

4. Check "ArgBlock PortStatusList"

5. Check <vendorname>

Test passed "MTU in OPERATE" = TRUE, and PDIn: PQI = 0xA0, InputDataLength = 0x20, PDI[0 … 31] = 0xFF, and PDInOut: PQI = 0xA0, OE = 0x00, InputDataLength = 0x20, PDI[0 … 31] = 0xFF, OutputDataLength = 0, and PortStatusList: PortQualityInfo = 0x02, RevisionID = 0x10, InputDataLength = 0x20, OutputDataLength = 0, and <vendorname> = "IO-Link Community"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report <vendorname> correct: <yes/no> <ok | nok>

Test Specification © IO-Link – 263 – Version V1.1.5

1981 1982

### Version V1.1.5 – 264 – Test Specification © IO-Link

### 1983 9.3.8 TYPE_2_V for 0/32 octets PD in/out and variable octets OD

### 1984 Table 201 defines the test conditions for this test case.

### 1985 Table 201 – TYPE_2_V for 0/32 octets PD in/out and variable octets OD

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0334

Name TCM_ DLPD_CYCC_ TYPE2VOD1IN0OUT32OCTET

Purpose (short) Master uses M-sequence TYPE_2_V for 0/32 octets PD in/out and variable ODs

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5,11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (rows 12, 15, 17, and 19)

Configuration / setup Master-Tester-System 1986

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master reads OD length = 1 and PD input length = 0 bits, Output length = 256 bits during STARTUP from Device. Master shall select M-sequence TYPE_2_V in OPER- ATE and propagate this information to SMI_PDIn, SMI_PDInOut and SMI_PortStatus services. Different OD lengths are tested within the test procedure loop (TPL).

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "Mscap" (Test parameter) b) MTU_DPP1_Set(M-sequenceCapability) = "Mscap";see B.1.4 in [1] c) MTU_DPP1_Set(PDIn length) = 0x00;”0” Bit d) MTU_DPP1_Set(PDOut length) = 0x100;”256” Bit e) SMI_PortConfiguration (ABPS_NOTYPE_CHECK);switch to OPERATE f) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE g) MTU_State_CheckOperate;returns "MTU in OPERATE" h) Evaluation 1) i) SMI_PDOut(ABPS_PDOUT<PDO[0..31]=0xFF>);PDO all bits “1” j) SMI_PDInOut;returns "ArgBlock PDInOut" k) Evaluation 2) l) MTU_PD_Get;returns "PDOut values" m) Evaluation 3) n) SMI_PortStatus;returns "ArgBlock PortStatusList" o) Evaluation 4) p) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>) q) Evaluation 5) r) SMI_PortConfiguration (ABPS_PORT_INACTIVE);SIO mode s) Repeat from b) with "Mscap" = next value from Test parameter

Test parameter Mscap = {0x19, 0x1B, 0x1D, 0x1F};M-sequence capability

Post condition – 1987

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "MTU in OPERATE"

2. Check "ArgBlock PDInOut"

3. Check "PDout values"

4. Check "ArgBlock PortStatusList"

5. Check <vendorname>

Test passed "MTU in OPERATE" = TRUE, and PDInOut: PQI = 0xA0, OE = 0x01, InputDataLength = 0x00, OutputDataLength = 0x20, PDO[0 … 31] = 0xFF, and PortStatusList: PortQualityInfo = 0x00, RevisionID = 0x11, InputDataLength = 0x20, OutputDataLength = 0, and <vendorname> = "IO-Link Community"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report <vendorname> correct: <yes/no> <ok | nok>

1988

1989

### Test Specification © IO-Link – 265 – Version V1.1.5

### 1990 9.3.9 TYPE_2_V for 32/0 octets PD in/out and variable octets OD

### 1991 Table 202 defines the test conditions for this test case.

### 1992 Table 202 – TYPE_2_V for 32/0 octets PD in/out and variable octets OD

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0335

Name TCM_ DLPD_CYCC_ TYPE2VOD1IN32OUT0OCTET

Purpose (short) Master uses M-sequence TYPE_2_V for 32/0 octets PD in/out and variable ODs

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5,11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (rows 13, 14, 16, and 18)

Configuration / setup Master-Tester-System 1993

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master reads OD length = 1 and PD input length = 256 bits, Output length = 0 bit during STARTUP from Device. Master shall select M-sequence TYPE_2_V in OPER- ATE and propagate this information to SMI_PDIn, SMI_PDInOut, and SMI_PortStatus services. Different OD lengths are tested within the test procedure loop (TPL).

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "Mscap" (Test parameter) b) MTU_DPP1_Set(M-sequenceCapability) = "Mscap";see B.1.4 in [1] c) MTU_DPP1_Set(PDIn length) = 0x100;"256" Bit d) MTU_DPP1_Set(PDOut length) = 0x00;"0" Bit e) MTU_PD_Set = 2^256-1;PDIn = all bits set f) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);switch to OPERATE g) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE h) MTU_State_CheckOperate;returns "MTU in OPERATE" i) Evaluation 1) j) SMI_PDIn;returns "ArgBlock PDIn" k) Evaluation 2) l) SMI_PDInOut;returns "ArgBlock PDInOut" m) Evaluation 3) n) SMI_PortStatus;returns "ArgBlock PortStatusList" o) Evaluation 4) p) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>) q) Evaluation 5) r) SMI_PortConfiguration (ABPS_PORT_INACTIVE);SIO mode s) Repeat from b) with "Mscap" = next value from Test parameter

Test parameter Mscap = {0x19, 0x1B, 0x1D, 0x1F};M-sequence capability

Post condition – 1994

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "MTU in OPERATE"

2. Check "ArgBlock PDIn"

3. Check "ArgBlock PDInOut"

4. Check "ArgBlock PortStatusList"

5. Check <vendorname>

Test passed "MTU in OPERATE" = TRUE, and PDIn: PQI = 0xA0, InputDataLength = 0x20, PDI[0 ... 31] = 0xFF, and PDInOut: PQI = 0xA0, OE = 0x00, InputDataLength = 0x20, PDI[0 ... 31] = 0xFF, OutputDataLength = 0, and PortStatusList: PortQualityInfo=0x02, RevisionID = 0x11, InputDataLength = 0x20, OutputDataLength = 0, and <vendorname> = "IO-Link Community"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report <vendorname> correct: <yes/no> <ok | nok>

1995

### Version V1.1.5 – 266 – Test Specification © IO-Link

### 1996 9.3.10 Master reads mirrored PD in/out from Device

### 1997 Table 203 defines the test conditions for this test case.

### 1998 Table 203 – Master reads mirrored PD in/out from Device

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0176

Name TCM_ DLPD_CYCC_MIRROREDPD

Purpose (short) Master reads mirrored Process Data in/out from Device

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 7.3.3.2, 9.2.3.5

Configuration / setup Master-Tester-System 1999

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Device (MTU) mirrors its PD such that the Master can check consistency.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x11;see B.1.4 in [1] b) MTU_DPP1_Set(PDIn length) = 0x10;”16” Bit c) MTU_DPP1_Set(PDOut length) = 0x10;”16” Bit d) SMI_PortConfiguration (ABPS_NOTYPE_CHECK);switch to OPERATE e) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE f) MTU_State_MirrorPD;perform PD mirroring g) SMI_PDOut(ABPS_PDOUT<OutputDataLength=2, PDO0=0x12, PDO1=0x34>) h) SMI_PDIn;returns "ArgBlock PDIn1" i) Evaluation 1) j) SMI_PDOut (ABPS_PDOUT<OutputDataLength=2, PDO0=0x56, PDO1=0x78>) k) SMI_PDIn;returns "ArgBlock PDIn2" l) Evaluation 2)

Test parameter –

Post condition – 2000

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "ArgBlock PDIn1"

2. Check "ArgBlock PDIn2"

Test passed PDIn1: PDI = [0x12, 0x34], and PDIn2: PDI = [0x56, 0x78]

Test not passed (examples) Inconsistency between transmitted and received process data

Report Sent and received PD match: <ok | nok>

2001

2002

### Test Specification © IO-Link – 267 – Version V1.1.5

### 2003 9.3.11 Master propagates "PD invalid" indication in a correct manner

### 2004 Table 204 defines the test conditions for this test case.

### 2005 Table 204 – Master propagates "PD invalid" indication in a correct manner

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0177

Name TCM_ DLPD_CYCC_PDINVALID

Purpose (short) Master propagates "PD invalid" indication in a correct manner

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

Specification (clause) [1], A.1.5 and Table A.5

Configuration / setup Master-Tester-System 2006

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) PDValid – PDInvalid transition reported correctly.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);switch to OPERATE b) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE c) SMI_PDIn;returns "ArgBlock PDIn_1" d) Evaluation 1) e) SMI_PortStatus;returns "ArgBlock PortStatusList_1" f) Evaluation 2) g) MTU_State_SetPDValidity(INVALID);PD invalid h) wait 500ms i) SMI_PDIn;returns "ArgBlock PDIn_2" j) Evaluation 3) k) SMI_PortStatus;returns "ArgBlock PortStatusList_2" l) Evaluation 4) m) SMI_PortConfiguration(ABPS_PORTINACTIVE);switch to Deactivated n) MTU_State_SetPDValidity(VALID);PD valid

Test parameter –

Post condition – 2007

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "ArgBlock PDIn_1"

2. Check "ArgBlock PortStatusList_1"

3. Check "ArgBlock PDIn_2"

4. Check "ArgBlock PortStatusList_2"

Test passed PDIn_1: PQI = 0xA0, and PortStatusList_1: PortQualityInfo.Bit0 = VALID, and PDIn_2: PQI = 0x20, and PortStatusList_2: PortQualityInfo.Bit0 = INVALID

Test not passed (examples) Master defines PDIn_1 in upper-level system as invalid and/or Master defines PDIn_2 in upper-level system as valid

Report "PDValidity" propagated correctly to upper-level system: <ok | nok>

2008

2009

### Version V1.1.5 – 268 – Test Specification © IO-Link

### 2010 9.3.12 Master propagates "PD valid" indication in a correct manner

### 2011 Table 205 defines the test conditions for this test case.

### 2012 Table 205 – Master propagates "PD valid" indication in a correct manner

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0178

Name TCM_ DLPD_CYCC_PDVALID

Purpose (short) Master propagates "PD Valid" indication in a correct manner

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

Specification (clause) [1], A.1.5 and Table A.5

Configuration / setup Master-Tester-System 2013

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) PDInvalid – PDValid transition reported correctly.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_SetPDValidity(INVALID);PD invalid b) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);switch to OPERATE c) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE d) MTU_State_SetPDValidity(VALID);PD valid e) wait 500ms f) SMI_PDIn;returns "ArgBlock PDIn" g) Evaluation 1) h) SMI_PortStatus;returns "ArgBlock PortStatusList" i) Evaluation 2)

Test parameter –

Post condition – 2014

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "ArgBlock PDIn"

2. Check "ArgBlock PortStatusList"

Test passed PDIn: PQI = 0xA0, and PortStatusList: PortQualityInfo.Bit0 = VALID

Test not passed (examples) Master defines the PD in the upper-level system as invalid

Report "PDInvalid" = "0" propagated to upper-level system: <ok | nok>

2015

2016

### Test Specification © IO-Link – 269 – Version V1.1.5

### 2017 9.4 On-request Data (OD)

### 2018 9.4.1 TYPE_2_V for different PD in/out and 1 octet OD

### 2019 Table 206 defines the test conditions for this test case.

### 2020 Table 206 – TYPE_2_V for different PD in/out and 1 octet OD

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0179

Name TCM_DLOD_CYCC_TYPE2VPDXOD1

Purpose (short) Master uses TYPE_2_V for different PD in/out and 1 octet OD

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5, 11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.4, A.2.6, B.1.4, E.4, E.10, E.11, E.12, Table A.10

Configuration / setup Master-Tester-System 2021

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check whether Master uses TYPE_2_V for several different PD in/out combinations and 1 octet OD. The Master receives the information about the target M-sequence type via address 0x03 (M-sequenceCapability), 0x05 (ProcessDataIn), and 0x06 (ProcessDataOut) out of DPP1. Different PD lengths are tested within a test procedure loop (TPL).

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_ISDU_Add(64, 0, 0x00);ISDU with value 1 x "0" b) Assign first value to "pdinlen", "pdoutlen", "mseq";Test parameter c) MTU_DPP1_Set(M-sequenceCapability) = "mseq";Test parameter d) MTU_DPP1_Set(PDIn length) = "pdinlen";"16" Bit e) MTU_DPP1_Set(PDOut length) = "pdoutlen";"0" Bit f) MTU_PD_Set = [0x5A,…, "pdinlen"-times];set “PDin” in MTU g) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);switch to OPERATE h) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE i) MTU_State_CheckOperate;returns "MTU in OPERATE" j) Evaluation 1) k) SMI_PDOut(ABPS_PDOUT<"pdoutlen", PDO0=0x5A>);set “PDout” l) SMI_DeviceWrite(ABPS_DEVICEWRITE<Index=64, OD=0xBF>) m) SMI_DeviceRead(ABPS_DEVICEREAD<Index=64>);returns "ArgBlock OD" n) Evaluation 2) o) MTU_PD_Get;returns "PDOut values" p) Evaluation 3) q) SMI_PDin;returns "ArgBlock PDIn" r) Evaluation 4) s) SMI_PortConfiguration(ABPS_PORTINACTIVE);deactivate Port t) Repeat from c) with next values of "pdinlen", "pdoutlen", "mseq"

Test parameter pdinlen = {1, 3, 32, 6, 0}, pdoutlen = {3, 1, 32, 0, 20};variable PD lengths mseq = {0x11, 0x11, 0x19, 0x19, 0x19};M-sequenceCapability

Post condition – 2022

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "MTU in OPERATE"

2. Check "ArgBlock OD"

3. Check "PDInOut values"

4. Check "ArgBlock PDin"

Test passed "MTU in OPERATE" = TRUE, and On-request Data = 0xBF, and PDout length = pdoutlen, all "PDOut values" = "0x5A", and PDin.InputDataLength = pdinlen, all PDin.PDIx = "0x5A"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report All M-sequence TYPEs (PD combinations): <ok | nok>

Version V1.1.5 – 270 – Test Specification © IO-Link

2023 2024

### Test Specification © IO-Link – 271 – Version V1.1.5

### 2025 9.4.2 TYPE_2_V for different PD in/out and 2 octets OD

### 2026 Table 207 defines the test conditions for this test case.

### 2027 Table 207 – TYPE_2_V for different PD in/out and 2 octets OD

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0180

Name TCM_DLOD_CYCC_TYPE2VPDXOD2

Purpose (short) Master uses TYPE_2_V for different PD in/out and 2 octets OD

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5, 11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.4, A.2.6, B.1.4, E.4, E.10, E.11, E.12, Table A.10

Configuration / setup Master-Tester-System 2028

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check whether Master uses TYPE_2_V for several different PD in/out combinations and 2 octets OD. The Master receives the information about the target M-sequence type via address 0x03 (M-sequenceCapability), 0x05 (ProcessDataIn), and 0x06 (ProcessDataOut) out of DPP1. Different PD lengths are tested within a test procedure loop (TPL).

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x2B;see B.1.4 in [1] b) MTU_ISDU_Add(64, 0, [0x00, 0x00]);ISDU with values 2 x "0" c) Assign first value to "pdinlen" and "pdoutlen";Test parameter d) MTU_DPP1_Set(PDIn length) = "pdinlen" × 8;in Bits e) MTU_DPP1_Set(PDOut length) = "pdoutlen" × 8;in Bits f) MTU_PD_Set = [0x5A,…, "pdinlen"-times];set “PDin” in MTU g) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);switch to OPERATE h) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE i) MTU_State_CheckOperate;returns "MTU in OPERATE" j) Evaluation 1) k) SMI_PDOut(ABPS_PDOUT<"pdoutlen",PDO0=0x5A>);set “PDout” l) SMI_DeviceWrite(ABPS_DEVICEWRITE<Index=64, OD=[0xAA, 0xBF]>) m) SMI_DeviceRead(ABPS_DEVICEREAD<Index=64>);returns "ArgBlock OD" n) Evaluation 2) o) MTU_PD_Get;returns "PDInOut values" p) Evaluation 3) q) SMI_PDin;returns "ArgBlock PDIn" r) Evaluation 4) s) SMI_PortConfiguration(ABPS_PORTINACTIVE);deactivate Port t) Repeat from d) with next values of "pdinlen", "pdoutlen";Test parameter

Test parameter pdinlen = {1, 32, 6, 0}, pdoutlen = {3, 32, 0, 20};variable PD lengths in octets

Post condition – 2029

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "MTU in OPERATE"

2. Check "ArgBlock OD"

3. Check "PDInOut values"

4. Check "ArgBlock PDin"

Test passed "MTU in OPERATE" = TRUE, and On-request Data = [0xAA, 0xBF], and PDout length = pdoutlen, all "PDOut values" = "0x5A", and PDin.InputDataLength = pdinlen, all PDin.PDIx = "0x5A"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report All M-sequence TYPEs (PD combinations): <ok | nok>

Version V1.1.5 – 272 – Test Specification © IO-Link

2030 2031

### Test Specification © IO-Link – 273 – Version V1.1.5

### 2032 9.4.3 TYPE_2_V for different PD in/out and 8 octets OD

### 2033 Table 208 defines the test conditions for this test case.

### 2034 Table 208 – TYPE_2_V for different PD in/out and 8 octets OD

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0181

Name TCM_DLOD_CYCC_TYPE2VPDXOD8

Purpose (short) Master uses TYPE_2_V for different PD in/out and 8 octets OD

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5, 11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.4, A.2.6, B.1.4, E.4, E.10, E.11, E.12, Table A.10

Configuration / setup Master-Tester-System 2035

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check whether Master uses TYPE_2_V for several different PD in/out combinations and 8 octets OD. The Master receives the information about the target M-sequence type via address 0x03 (M-sequenceCapability), 0x05 (ProcessDataIn), and 0x06 (ProcessDataOut) out of DPP1. Different PD lengths are tested within a test procedure loop (TPL).

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x2D;see B.1.4 in [1] b) MTU_ISDU_Add(64, 0, [0x00, …,0x00]);ISDU with values 8 x "0" c) Assign first value to "pdinlen" and "pdoutlen";Test parameter d) MTU_DPP1_Set(PDIn length) = "pdinlen" × 8;in Bits e) MTU_DPP1_Set(PDOut length) = "pdoutlen" × 8;in Bits f) MTU_PD_Set = [0x5A,…, "pdinlen"-times];set “PDin” in MTU g) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);switch to OPERATE h) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE i) MTU_State_CheckOperate;returns "MTU in OPERATE" j) Evaluation 1) k) SMI_PDOut(ABPS_PDOUT<"pdoutlen", PDO0=0x5A>);set “PDout” l) SMI_DeviceWrite(ABPS_DEVICEWRITE<Index=64, OD=[0xAA, 0xBF, 0x77, 0x40, 0x55, 0xCD, 0x33, 0xE4]>) m) SMI_DeviceRead(ABPS_DEVICEREAD<Index=64>);returns "ArgBlock OD" n) Evaluation 2) o) MTU_PD_Get;returns "PDInOut values" p) Evaluation 3) q) SMI_PDin;returns "ArgBlock PDIn" r) Evaluation 4) s) SMI_PortConfiguration(ABPS_PORTINACTIVE);deactivate Port t) Repeat from d) with next values of "pdinlen", "pdoutlen";Test parameter

Test parameter pdinlen = {1, 32, 6, 0}, pdoutlen = {3, 32, 0, 20};variable PD lengths in octets

Post condition – 2036

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "MTU in OPERATE"

2. Check "ArgBlock OD"

3. Check "PDInOut values"

4. Check "ArgBlock PDin"

Test passed "MTU in OPERATE" = TRUE, and On-request Data = [0xAA, 0xBF, 0x77, 0x40, 0x55, 0xCD, 0x33, 0xE4], and PDout length = pdoutlen, all "PDOut values" = "0x5A", and PDin.InputDataLength = pdinlen, all PDin.PDIx = "0x5A"

Test not passed (examples) Any of the checks in Test passed failed or communication error

### Version V1.1.5 – 274 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

Report All M-sequence TYPEs (PD combinations): <ok | nok>

### 2037 9.4.4 TYPE_2_V for different PD in/out and 32 octets OD

### 2038 Table 209 defines the test conditions for this test case.

### 2039 Table 209 – TYPE_2_V for different PD in/out and 32 octets OD

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0182

Name TCM_DLOD_CYCC_TYPE2VPDXOD32

Purpose (short) Master uses TYPE_2_V for different PD in/out and 32 octets OD

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 9.2.3.5, 11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.4, A.2.6, B.1.4, E.4, E.10, E.11, E.12, Table A.10

Configuration / setup Master-Tester-System 2040

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check whether Master uses TYPE_2_V for several different PD in/out combinations and 32 octets OD. The Master receives the information about the target M-sequence type via address 0x03 (M-sequenceCapability), 0x05 (ProcessDataIn), and 0x06 (ProcessDataOut) out of DPP1. Different PD lengths are tested within a test procedure loop (TPL).

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x2F;see B.1.4 in [1] b) MTU_ISDU_Add(64, 0, [0x00, …,0x00]);ISDU with values 32 x "0" c) Assign first value to "pdinlen" and "pdoutlen";Test parameter d) MTU_DPP1_Set(PDIn length) = "pdinlen" × 8;in Bits e) MTU_DPP1_Set(PDOut length) = "pdoutlen" × 8;in Bits f) MTU_PD_Set = [0x5A,…, "pdinlen"-times];set “PDin” in MTU g) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);switch to OPERATE h) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE i) MTU_State_CheckOperate;returns "MTU in OPERATE" j) Evaluation 1) k) SMI_PDOut(ABPS_PDOUT<"pdoutlen",PDO0= 0x5A>);set “PDout” l) SMI_DeviceWrite(ABPS_DEVICEWRITE<Index=64, OD=[ODstring]>);Test parameter m) SMI_DeviceRead(ABPS_DEVICEREAD<Index=64>);returns "ArgBlock OD" n) Evaluation 2) o) MTU_PD_Get;returns "PDInOut values" p) Evaluation 3) q) SMI_PDin;returns "ArgBlock PDIn" r) Evaluation 4) s) SMI_PortConfiguration(ABPS_PORTINACTIVE);deactivate Port t) Repeat from d) with next values of "pdinlen", "pdoutlen";Test parameter

Test parameter pdinlen = {1, 32, 6, 0}, pdoutlen = {3, 32, 0, 20};variable PD lengths in octets ODstring = "Hello World, this is Master Test"

Post condition – 2041

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "MTU in OPERATE"

2. Check "ArgBlock OD"

3. Check "PDInOut values"

4. Check "ArgBlock PDin"

Test passed "MTU in OPERATE" = TRUE, and On-request Data = ["Hello World, this is Master Test"], and PDout length = pdoutlen, all "PDOut values" = "0x5A", and PDin.InputDataLength = pdinlen, all PDin.PDIx = "0x5A"

### Test Specification © IO-Link – 275 – Version V1.1.5

TEST CASE RESULTS CHECK / REACTION

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report All M-sequence TYPEs (PD combinations): <ok | nok>

2042

### Version V1.1.5 – 276 – Test Specification © IO-Link

### 2043 9.4.5 Master sends Idle after an accomplished ISDU service

### 2044 Table 210 defines the test conditions for this test case.

### 2045 Table 210 – Master sends Idle after an accomplished ISDU service

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0288

Name TCM_LGCY_MANY_IDLEAFTERISDU

Purpose (short) Master sends ISDU request "IDLE 1" after an accomplished ISDU service

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master M-sequence test: test to pass

Specification (clause) [1], 7.3.6.3, Figure 51 (Transition T8)

Configuration / setup Master-Tester-System 2046

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) After completion of an ISDU service, the Master shall transmit at least one "IDLE 1" request to the Device as an acknowledgement of the ISDU service (I-Service).

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(MasterCycleTime) = 0xBF;cycle time = 132.8ms b) MTU_ISDU_Add(64, 0, [0xAA]);ISDU with length "1" c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);switch to OPERATE d) TM_AWAIT_PORT_STATUS(OPERATE);wait for OPERATE e) MTU_ISDU_TrailingIdles;reset "NumIdles" f) SMI_DeviceRead(ABPS_DEVICEREAD <Index=64>);start second Read g) SMI_DeviceRead(ABPS_DEVICEREAD<Index=64>);as soon as possible h) MTU_ISDU_TrailingIdles;return "NumIdles" i) Evaluation 1)

Test parameter –

Post condition / next test – 2047

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check value of "NumIdles"

Test passed "NumIdles" > 0

Test not passed (examples) "NumIdles" = 0

Report IDLE 1 message: <yes/no> <ok | nok>

2048

2049

### Test Specification © IO-Link – 277 – Version V1.1.5

### 2050 9.5 STARTUP

### 2051 9.5.1 Master reads communication parameters (Direct Parameter)

### 2052 Table 211 defines the test conditions for this test case.

### 2053 Table 211 – Master reads communication parameters (Direct Parameter)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0183

Name TCM_DLST_CHCK_COMPARAM

Purpose (short) Check that Master starts communication and reads communication parameters

Equipment under test (EUT) Master

Test case version 1.3

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.2, 9.3.3, Figures 71, 72, and 81

Configuration / setup Master-Tester-System 2054

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check correct start-up of Master comprising "V1.0" support. Master reads communication parameter address 0x02 to 0x06 (Direct Parameter page 1).

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "com" b) MTU_Timing_SetCommunicationMode = "com" c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) d) TM_AWAIT_PORT_STATUS(OPERATE) e) MTU_Startup_GetLog;returning MessageLog f) Evaluation 1) g) SMI_PortConfiguration(ABPS_PORTINACTIVE) h) Repeat from b) with next "com"

Test parameter com = {COM1, COM2, COM3}

Post condition – 2055

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

Test passed For all COMx: MessageLog shall fulfil the following requirements:

- No communication retries

- Master read messages to parameters on address 0x02 to 0x06 in ascending order

- One or two Master read messages to parameter on address 0x02 permitted

- Only one Master read message to each parameter on addresses 0x03 to 0x06

Test not passed (examples) Any of the requirements not fulfilled at any COMx

Report No communication retries: <ok | nok> Master read messages in ascending order: <ok | nok> Number of read accesses to address 0x02: <value> <ok | nok> Number of read accesses to addresses 0x03 to 0x06 <ok | nok> Communication modes COM1, COM2, COM3: <ok | nok> NOTE In this test case, a Master read on address 0x02 only once is tolerated; however, the Master-Tester-System shall display and/or print out a warning.

2056

2057

### Version V1.1.5 – 278 – Test Specification © IO-Link

### 2058 9.5.2 Master adjusts to protocol V1.1 (Direct Parameter)

### 2059 Table 212 defines the test conditions for this test case.

### 2060 Table 212 – Master adjusts to protocol V1.1 (Direct Parameter)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0184

Name TCM_DLST_CHCK_VIDDID

Purpose (short) Check whether Master adjusts to protocol V1.1 and reads VendorID and DeviceID

Equipment under test (EUT) Master

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.2, 9.2.3.3, Figures 71 to 75, and 81

Configuration / setup Master-Tester-System 2061

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check whether Master adjusts to protocol revision V1.1 and reads VendorID and DeviceID. Check for the right decision with respect to the protocol revision.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) SMI PortConfiguration(ABPS_NOTYPE_CHECK) b) TM_AWAIT_PORT_STATUS(OPERATE) c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1)

Test parameter –

Post condition – 2062

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

Test passed MessageLog shall contain after the sequence of reading parameter addresses 0x02 to 0x06 the following sequence of messages in this order:

- Write MasterCommand 0x95 (Master_Ident)

- Read VendorID

- Read DeviceID

Test not passed (examples) Master did not write MasterCommand 0x95

Report MasterCommand 0x95: <yes/no> <ok | nok> Master reads VendorID: <value> <ok | nok> Master reads DeviceID: <value> <ok | nok>

2063

2064

### Test Specification © IO-Link – 279 – Version V1.1.5

### 2065 9.5.3 Master adjusts to protocol V1.0 (Direct Parameter)

### 2066 Table 213 defines the test conditions for this test case.

### 2067 Table 213 – Master adjusts to protocol V1.0 (Direct Parameter)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0185

Name TCM_DLST_CHCK_V10VIDDID

Purpose (short) Check whether Master adjusts to protocol V1.0 and reads VendorID and DeviceID

Equipment under test (EUT) Master

Test case version 1.2

Category / type Master protocol test: test to pass

Specification (clause), 9.2.3.2, 9.2.3.3, Figures 71 to 75, and 81

Configuration / setup Master-Tester-System 2068

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check whether Master adjusts to protocol revision V1.0 and reads VendorID and DeviceID. Check for the right decision with respect to the protocol revision.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE MTU_DPP1_Set(RevisionID) = 0x10;Set "Device" to revision V1.0

Procedure a) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) b) TM_AWAIT_PORT_STATUS(OPERATE) c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1)

Test parameter –

Post condition – 2069

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

Test passed MessageLog shall contain after the sequence of reading parameter addresses 0x02 to 0x06 the following sequence of messages in this order:

- No Write MasterCommand 0x95 (Master_Ident)

- Read VendorID

- Read DeviceID

Test not passed (examples) Master did not recognize the correct protocol version and wrote MasterCommand 0x95 and/or did not read the VendorID and DeviceID.

Report No MasterCommand 0x95: <yes/no> <ok | nok> Master reads VendorID: <value> <ok | nok> Master reads DeviceID: <value> <ok | nok>

2070

2071

### Version V1.1.5 – 280 – Test Specification © IO-Link

### 2072 9.5.4 Master start-up with non configured VID and DID

### 2073 Table 214 defines the test conditions for this test case.

### 2074 Table 214 – Master start-up with non configured VID and DID

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0186

Name TCM_DLST_CHCK_NONCONFVIDDID

Purpose (short) Check whether Master performs start-up with non-configured VID and DID

Equipment under test (EUT) Master

Test case version 1.3

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.2, 9.2.3.3, Figures 71 to 75, and 81

Configuration / setup Master-Tester-System 2075

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Master behavior. Master establishes communication with Device and turns it into OPERATE. Configured PortMode = IOL_AUTOSTART and Validation&Backup = No Device check.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) b) TM_AWAIT_PORT_STATUS(OPERATE) c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1) e) SMI_PortStatus;returning PortStatusList f) Evaluation 2)

Test parameter –

Post condition – 2076

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

2. Check PortStatusList

Test passed In 1) MessageLog shall contain after writing MasterCommand 0x95 (Master_Ident), the following sequence of messages in this order:

- Read VendorID

- Read DeviceID

- Read FunctionID

- Write MasterCommand 0x9A;Device Preoperate

In 2) PortStatusList shall show the following values: PortStatusList.PortStatusInfo = OPERATE PortStatusList.DeviceID = 0x002BD2

Test not passed (examples) Master does not write MasterCommand 0x9A

Report Master reads VendorID: <value> <ok | nok> Master reads DeviceID: <value> <ok | nok> Master reads FunctionID: <value> <ok | nok> MasterCommand 0x95: <yes/no> <ok | nok> Correct values in PortStatusList: <yes/no> <ok | nok>

2077

2078

### Test Specification © IO-Link – 281 – Version V1.1.5

### 2079 9.5.5 Master start-up with configured VID and DID

### 2080 Table 215 defines the test conditions for this test case.

### 2081 Table 215 – Master start-up with configured VID and DID

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0187

Name TCM_DLST_CHCK_CONFVIDDID

Purpose (short) Check Master start-up behavior with configured VendorID and DeviceID

Equipment under test (EUT) Master

Test case version 1.4

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.2, 9.2.3.3, Figures 71 to 75, and 81

Configuration / setup Master-Tester-System 2082

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Master start-up behavior. Master establishes communication with Device and turns it into OPERATE. Configured PortMode = IOL_MANUAL and Validation&Backup = Type compatible Device V1.1.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) SMI_PortConfiguration(ABPS_TYPE_COMP);type compatibility b) TM_AWAIT_PORT_STATUS(OPERATE) c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1) e) SMI_PortStatus;returning PortStatusList f) Evaluation 2)

Test parameter –

Post condition – 2083

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

2. Check PortStatusList

Test passed In 1) MessageLog shall contain after writing MasterCommand 0x95 (Master_Ident), the following sequence of messages in this order:

- Read VendorID

- Read DeviceID

- Read FunctionID

- Write MasterCommand 0x9A;Device Preoperate

In 2) PortStatusList shall show the following values: PortStatusList.PortStatusInfo = OPERATE PortStatusList.DeviceID = 0x002BD2

Test not passed (examples) Master does not write MasterCommand 0x9A, or values in PortStatusList are not correct

Report Master reads VendorID: <value> <ok | nok> Master reads DeviceID: <value> <ok | nok> Master reads FunctionID: <value> <ok | nok> MasterCommand 0x95: <yes/no> <ok | nok> Correct values in PortStatusList: <yes/no> <ok | nok>

2084

2085

### Version V1.1.5 – 282 – Test Specification © IO-Link

### 2086 9.5.6 Master start-up with overwrite of the DID (compatible)

### 2087 Table 216 defines the test conditions for this test case.

### 2088 Table 216 – Master start-up with overwrite of the DID (compatible)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0188

Name TCM_DLST_CHCK_OVERDIDOK

Purpose (short) Check Master start-up behavior with overwrite of DeviceID (compatible)

Equipment under test (EUT) Master

Test case version 1.6

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.2, 9.3.3, Figures 71 to 76, and 81

Configuration / setup Master-Tester-System 2089

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Master start-up behavior with overwrite of DeviceID (compatible). Master establishes communication with the MTU, detects incorrect "Device", overwrites the DeviceID with requested DeviceID, reads communication parameters again, and turns MTU into OPERATE. Configured PortMode = IOL_MANUAL and Validation&Backup = Type compatible Device V1.1. NOTE MTU shall tolerate Masters reading communication parameter address 0x02 (Direct Parameter page 1) twice.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE MTU_DPP1_Set(DeviceID) = 0x00A439

Procedure a) SMI_PortConfiguration(ABPS_TYPE_COMP);type compatibility b) TM_AWAIT_PORT_STATUS(OPERATE) c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1) e) SMI_PortStatus;returning PortStatusList f) Evaluation 2)

Test parameter –

Post condition – 2090

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

2. Check PortStatusList

Test passed MessageLog shall contain after writing MasterCommand 0x95 (Master_Ident) the following sequence of messages in this order: Read VendorID → Read DeviceID → Read FunctionID → Write Revision = "0x11" → Write configured DeviceID (0x002BD2) → Write MasterCommand 0x96 → Read communication parameter on address 0x02 to 0x06 → Read again VendorID → Read again DeviceID → Read again FunctionID → Write MasterCommand 0x9A, and PortStatusList: PortStatusInfo = OPERATE, DeviceID = 0x002BD2

Test not passed (examples) Master does not write MasterCommand 0x9A, or DeviceID of MTU ≠ 0x002BD2, or values in PortStatusList are not correct

Report Master writes Revision 0x11: <yes/no> <ok | nok> MasterCommand 0x96: <yes/no> <ok | nok> Master reads VendorID: <value> <ok | nok> Master reads DeviceID: <value> <ok | nok> Master reads FunctionID: <value> <ok | nok> MasterCommand 0x9A: <yes/no> <ok | nok> Correct values in PortStatusList: <yes/no> <ok | nok>

2091 2092

### Test Specification © IO-Link – 283 – Version V1.1.5

### 2093 9.5.7 Master start-up with overwrite of the DID (incompatible)

### 2094 Table 217 defines the test conditions for this test case.

### 2095 Table 217 – Master start-up with overwrite of the DID (incompatible)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0189

Name TCM_DLST_CHCK_OVERDIDNOK

Purpose (short) Check Master start-up behavior with overwrite of the DeviceID (incompatible)

Equipment under test (EUT) Master

Test case version 1.5

Category / type Master protocol test: test to pass

### Specification (clause) [1], 9.2.3.2, 9.3.3, Figures 71 to 76, and 81

Configuration / setup Master-Tester-System 2096

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Master start-up behavior with overwrite of the DeviceID. This test case supposes an incompatible DeviceID causing MTU to initiate a corresponding Event. Master starts communication with MTU, detects incorrect "Device", overwrites the DeviceID with the requested DeviceID, reads communication parameters again, and turns MTU into PREOPERATE. MTU shall tolerate Master reading communication parameter address 0x02 (Direct Parameter page

1. twice.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE MTU_DPP1_Set(DeviceID) = 0x00AAAA;incompatible to any other DID

Procedure a) SMI_PortConfiguration(ABPS_TYPE_COMP);type compatibility b) TM_AWAIT_PORT_STATUS(PORT_DIAG) c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1) e) SMI_PortEvent;returning EventCode f) Evaluation 2) g) SMI_PortStatus;returning PortStatusList h) Evaluation 3)

Test parameter –

Post condition – 2097

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

2. Check EventCode

3. Check PortStatusList

Test passed MessageLog shall contain after writing MasterCommand 0x95 (Master_Ident), the following sequence of messages in this order: Read VendorID → Read DeviceID → Read FunctionID → Write Revision=0x11 → Write configured DeviceID → Write MasterCommand 0x96 → Read communication parameter on address 0x02 to 0x06 → Read again VendorID → Read again DeviceID → Read again FunctionID → Write MasterCommand 0x9A → No Write MasterCommand 0x99, and PortEvent: EventCode = 0x1803, and;"Incorrect DeviceID" PortStatusList: PortStatusInfo = PORT_DIAG, DeviceID = 0x00AAAA, DiagEntry[any].EventCode = 0x1803;"Incorrect DeviceID"

Test not passed (examples) No PORT_DIAG, or MasterCommand 0x9A and thereafter a MasterCommand 0x99

Report Master writes Revision 0x11: <yes/no> <ok | nok> Master writes configured DeviceID: <yes/no> <ok | nok> MasterCommand 0x96: <yes/no> <ok | nok> Master reads VendorID: <value> <ok | nok> Master reads DeviceID: <value> <ok | nok> MasterCommand 0x9A: <yes/no> <ok | nok> Master reads FunctionID: <value> <ok | nok> No MasterCommand 0x99: <yes/no> <ok | nok>

### Version V1.1.5 – 284 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION Master indicates Event: <EventCode> <ok | nok> Correct values in PortStatusList: <yes/no> <ok | nok>

### 2098 9.5.8 Master start-up with overwrite of the RID (incompatible)

### 2099 Table 218 defines the test conditions for this test case.

### 2100 Table 218 – Master start-up with overwrite of the RID (incompatible)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0190

Name TCM_DLST_CHCK_OVERRIDNOK

Purpose (short) Check Master start-up behavior with overwrite of the RevisionID (incompatible)

Equipment under test (EUT) Master

Test case version 1.6

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.2, 9.3.3, Figures 71 to 76, and 81

Configuration / setup Master-Tester-System 2101

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Master start-up behavior with overwrite of RevisionID (RID). This test case supposes an incompatible RevisionID (>V1.1). Master starts communication with MTU, detects the "incorrect" RevisionID, and overwrites the RevisionID with the requested RevisionID. MTU in turn restores the original RID causing a corresponding Event. MTU shall tolerate Master reading communication parameter address 0x02 (Direct Parameter page 1) twice.

Precondition EUT: PORT_INACTIVE (IOL_MANUAL, Type compatible Device V1.1) MTU: MTU_STANDARD_STATE with MTU_DPP1_Set(DeviceID) = 0x00A439, and MTU_DPP1_Set(RevisionID) = 0x12;incorrect RevisionID (MTU does not ;accept any other protocol revision in this state)

Procedure a) SMI_PortConfiguration(ABPS_TYPE_COMP);type compatibility

### b) TM_AWAIT_PORT_STATUS(PORT_DIAG))

c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1) e) SMI_PortEvent;returning EventCode f) Evaluation 2) g) SMI_PortStatus;returning PortStatusList h) Evaluation 3)

Test parameter –

Post condition – 2102

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

2. Check EventCode

2. Check PortStatusList

Test passed MessageLog shall contain after writing MasterCommand 0x95 (Master_Ident), the following sequence of messages in this order: Write Revision = "0x11" → Write configured DeviceID → Write MasterCommand 0x96 → Read communication parameter on address 0x02 to 0x06 → MasterCommand 0x95 (Master_Ident) → Write MasterCommand 0x9A → No Write MasterCommand 0x99, and;Device Operate PortEvent: EventCode = 0x1801, and;"Startup Parameterization error" PortStatusList: PortStatusInfo = PORT_DIAG, DeviceID = 0x00A439, RevisionID = 0x12, DiagEntry[any].EventCode = 0x6001;"Startup parametrization error"

Test not passed (examples) Master did not send MasterCommand 0x9A, or Master leaves PREOPERATE state.

Report Master writes Revision 0x11: <yes/no> <ok | nok> Master writes configured DeviceID: <yes/no> <ok | nok> MasterCommand 0x96: <yes/no> <ok | nok> MasterCommand 0x9A: <yes/no> <ok | nok>

### Test Specification © IO-Link – 285 – Version V1.1.5

TEST CASE RESULTS CHECK / REACTION No MasterCommand 0x99: <yes/no> <ok | nok> Master indicates Event: <EventCode> <ok | nok> Correct values in PortStatusList: <yes/no> <ok | nok>

2103

### Version V1.1.5 – 286 – Test Specification © IO-Link

### 2104 9.5.9 Master start-up with non configured VID and DID (V1.0)

### 2105 Table 219 defines the test conditions for this test case.

### 2106 Table 219 – Master start-up with non configured VID and DID (V1.0)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0192

Name TCM_DLST_CHCK_VIDDIDNONCONFIG

Purpose (short) Check Master start-up behavior with non-configured VID and DID (V1.0)

Equipment under test (EUT) Master

Test case version 1.5

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.2, 9.3.3, Figures 71 to 76, and 81

Configuration / setup Master-Tester-System 2107

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Master start-up behavior with non-configured VID and DID for a legacy Device (V1.0). The Master establishes communication with the "Device" (MTU), writes the MasterCycleTime (address 0x01 in Direct Parameter page 1), and sends MasterCommand 0x99 (OPERATE) turning the "Device" from STARTUP to OPERATE. Configured PortMode = IOL_AUTOSTART and Validation&Backup = No Device check.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE MTU_DPP1_Set(RevisionID) = 0x10;Revision V1.0

Procedure a) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);no type check b) TM_AWAIT_PORT_STATUS(OPERATE) c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1) e) SMI_PortStatus;returning PortStatusList f) Evaluation 2)

Test parameter –

Post condition – 2108

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

2. Check PortStatusList

Test passed MessageLog shall contain after read of parameter address 0x02 to 0x06 the following sequence of messages in this order: Read VendorID → Read DeviceID → Read FunctionID → Write MasterCycleTime → Write MasterCommand 0x99 PortStatusList: PortStatusInfo = OPERATE, DeviceID = 0x002BD2

Test not passed (examples) Master does not write the MasterCycleTime (address 0x01) or does not send MasterCommand 0x99

Report Master reads VendorID: <value> <ok | nok> Master reads DeviceID: <value> <ok | nok> Master reads FunctionID: <value> <ok | nok> MasterCycleTime written: <yes/no> <ok | nok> MasterCommand 0x99: <yes/no> <ok | nok>

2109

2110

### Test Specification © IO-Link – 287 – Version V1.1.5

### 2111 9.5.10 Master start-up with configured VID and DID (Device V1.0)

### 2112 Table 220 defines the test conditions for this test case.

### 2113 Table 220 – Master start-up with configured VID and DID (V1.0)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0193

Name TCM_DLST_CHCK_VIDDIDCONFIG

Purpose (short) Check Master start-up behavior with configured VID and DID (Device V1.0)

Equipment under test (EUT) Master

Test case version 1.5

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.2, 9.3.3, Figures 71 to 76, and 81

Configuration / setup Master-Tester-System 2114

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Master start-up behavior with configured VID and DID for a legacy "Device" (V1.0). Master establishes communication with the "Device" (MTU) and writes the MasterCycleTime (address 0x01 in Direct Parameter page 1) only if the configured values match the values in the "Device". Then, the Master shall turn the "Device" from STARTUP into OPERATE via MasterCommand 0x99. Configured PortMode = IOL_MANUAL and Validation&Backup = Type compatible Device V1.0

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE MTU_DPP1_Set(RevisionID) = 0x10;"Device" revision V1.0

Procedure a) SMI_PortConfiguration(ABPS_TYPE_COMP< Validation&Backup= TYPE_compatible_Device_V1.0>);type compatibility b) TM_AWAIT_PORT_STATUS(OPERATE) c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1) e) SMI_PortStatus;returning PortStatusList f) Evaluation 2)

Test parameter –

Post condition – 2115

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

2. Check PortStatusList

Test passed MessageLog shall contain after read of parameter address 0x02 to 0x06 the following sequence of messages in this order: Read VendorID → Read DeviceID → Read FunctionID → Write MasterCycleTime → Write MasterCommand 0x99 PortStatusList: PortStatusInfo = OPERATE, DeviceID = 0x002BD2

Test not passed (examples) Master does not write the MasterCycleTime (address 0x01) or does not send MasterCommand 0x99

Report Master reads VendorID: <value> <ok | nok> Master reads DeviceID: <value> <ok | nok> Master reads FunctionID: <value> <ok | nok> MasterCycleTime written: <yes/no> <ok | nok> MasterCommand 0x99: <yes/no> <ok | nok>

2116

2117

### Version V1.1.5 – 288 – Test Specification © IO-Link

### 2118 9.5.11 Master start-up with incorrect DID (Device V1.0)

### 2119 Table 221 defines the test conditions for this test case.

### 2120 Table 221 – Master start-up with incorrect DID (V1.0)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0194

Name TCM_DLST_CHCK_DIDWRONG

Purpose (short) Check Master start-up behavior with incorrect DID (Device V1.0)

Equipment under test (EUT) Master

Test case version 1.5

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.2, 9.3.3, Figures 71 to 76, and 81

Configuration / setup Master-Tester-System 2121

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Master start-up behavior with incorrect configured DID for a legacy "Device" (V1.0). Master establishes communication with MTU, recognizes a deviating DeviceID, and indicates a corresponding Event. Configured PortMode = IOL_MANUAL and Validation&Backup = Type compatible Device V1.0

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE MTU_DPP1_Set(RevisionID) = 0x10;Revision V1.0 MTU_DPP1_Set(DeviceID) = 0x00AAAA;incompatible to any other DID

Procedure a) SMI_PortConfiguration(ABPS_TYPE_COMP<Validation&Backup= TYPE_compatible_Device_V1.0>);type compatibility b) TM_AWAIT_PORT_STATUS(PORT_DIAG) c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1) e) SMI_PortEvent;returning EventCode f) Evaluation 2) g) SMI_PortStatus;returning PortStatusList h) Evaluation 3)

Test parameter –

Post condition – 2122

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

2. Check EventCode

2. Check PortStatusList

Test passed MessageLog shall contain after read of parameter address 0x02 to 0x06, the following sequence of messages in this order: Read VendorID -> Read DeviceID -> Read FunctionID -> Write MasterCycleTime -> Write MasterCommand 0x99 -> No Write MasterCommand 0x98 PortEvent: EventCode = 0x1803;"Incorrect DeviceID" PortStatusList: PortStatusInfo = PORT_DIAG, DeviceID = 0x00AAAA, DiagEntry[any].EventCode = 0x1803;"Incorrect DeviceID"

Test not passed (examples) Master indicates no system specific fault information or sends MasterCommand 0x98.

Report Master reads VendorID: <value> <ok | nok> Master reads DeviceID: <value> <ok | nok> Master reads FunctionID: <value> <ok | nok> No MasterCommand 0x98: <yes/no> <ok | nok> Master indicates Event: <EventCode> <ok | nok> Correct values in PortStatusList: <yes/no> <ok | nok>

2123

2124

### Test Specification © IO-Link – 289 – Version V1.1.5

### 2125 9.5.12 Master start-up with overwrite of the RID (compatible)

### 2126 Table 222 defines the test conditions for this test case.

### 2127 Table 222 – Master start-up with overwrite of the RID (compatible)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0307

Name TCM_DLST_CHCK_OVERRIDOK

Purpose (short) Check Master start-up behavior with overwrite of the Device RID (compatible)

Equipment under test (EUT) Master

Test case version 1.3

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.2, 9.3.3, 10.6.3, Figures 71 to 76, and 81

Configuration / setup Master-Tester-System 2128

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Check Master start-up behavior with overwrite of the RevisionID (compatible). This test supposes the active switching to protocol revision 1.0. Master overwrites the RID and the "Device" accepts the requested protocol revision. Configured PortMode = IOL_MANUAL and Validation&Backup = Type compatible Device V1.0 NOTE MTU shall tolerate Master reading communication parameter address 0x02 (Direct Parameter page 1) twice.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE;MTU accepts protocol revision 1.0 in this state

Procedure a) SMI_PortConfiguration(ABPS_TYPE_COMP, RevisionID = "CRID") (Test param.) b) TM_AWAIT_PORT_STATUS(OPERATE) c) MTU_Startup_GetLog;returning MessageLog d) Evaluation 1) e) SMI_PortStatus;returning PortStatusList f) Evaluation 2)

Test parameter CRID = 0x10;configured RevisionID V1.0

Post condition – 2129

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check MessageLog

2. Check PortStatusList

Test passed MessageLog shall contain after writing MasterCommand 0x95 (Master_Ident) the following sequence of messages in this order: Write RevisionID = "0x10" → Write DeviceID → Write MasterCommand 0x96 → Read communication parameter on address 0x02 to 0x06 → Read VendorID → Read DeviceID → Read FunctionID Write MasterCommand 0x99;DeviceOperate PortStatusList: PortStatusInfo = OPERATE, DeviceID = 0x002BD2, RevisionID = "CRID"

Test not passed (examples) Master writes MasterCommand 0x9A or MTU does not show RevisionID 0x10

Report Master writes RevisionID 0x10: <yes/no> <ok | nok> Master writes configured DeviceID: <yes/no> <ok | nok> MasterCommand 0x96: <yes/no> <ok | nok> Master reads DPP1 parameters 0x02 to 0x06: <yes/no> <ok | nok> Master reads VendorID: <value> <ok | nok> Master reads DeviceID: <value> <ok | nok> Master reads FunctionID: <value> <ok | nok> No MasterCommand 0x9A: <yes/no> <ok | nok> Correct values in PortStatusList: <yes/no> <ok | nok>

2130

### Version V1.1.5 – 290 – Test Specification © IO-Link

### 2131 9.6 PREOPERATE

### 2132 9.6.1 Master in PREOPERATE uses TYPE_0 to read Index 24 (1 OD)

### 2133 Table 223 defines the test conditions for this test case.

### 2134 Table 223 – Master in PREOPERATE uses TYPE_0 to read Index 24 (1 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0202

Name TCM_DLOD_PREP_TYPE0READOD1

Purpose (short) Master uses TYPE_0 to read Index 24 in PREOPERATE (1 octet OD)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.2, A.5.2, B.2.1, Tables A.8, A.13

Configuration / setup Master-Tester-System 2135

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for PREOPERATE. TYPE_0 with 1 octet OD shall be used for Read access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x01;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength) = 0 c) MTU_DPP1_Set(OutputLength) = 0 d) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID e) MTU_ISDU_Add(Index = 24, Subindex = 0, Value = testdata);Test parameter f) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns "Reached" g) Evaluation 1) h) SMI_DeviceRead(ABPS_DEVICEREAD(Index = 24, Subindex = 0));returns "OD" i) Evaluation 2) j) MTU_State_CheckOperate;returns "MTU in OPERATE" k) Evaluation 3)

Test parameter testdata = "0x74, 0x65, 0x73, 0x74, 0x20, 0x6F, 0x66, 0x20, 0x74, 0x68, 0x65, 0x20, 0x6D, 0x61, 0x73, 0x74, 0x65, 0x72, 0x73, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x33, 0x32, 0x20, 0x6F, 0x64, 0x64, 0x61"

Post condition – 2136

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Reached"

2. Check "OD"

3. Check "MTU in OPERATE"

Test passed "Reached" = TRUE, and "OD" = testdata, and "MTU in OPERATE" = FALSE

Test not passed (examples) Any evaluation failed or communication error

Report All evaluations <ok | nok>

2137

2138

### Test Specification © IO-Link – 291 – Version V1.1.5

### 2139 9.6.2 Master in PREOPERATE uses TYPE_1_2 to read Index 24 (2OD)

### 2140 Table 224 defines the test conditions for this test case.

### 2141 Table 224 – Master in PREOPERATE uses TYPE_1_2 to read Index 24 (2OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0203

Name TCM_DLOD_PREP_TYPE12READOD2

Purpose (short) Master uses TYPE_1_2 to read Index 24 in PREOPERATE (2 octets OD)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.3, A.5.2, B.2.1, Tables A.8, A.13

Configuration / setup Master-Tester-System 2142

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for PREOPERATE. TYPE_1-2 with 2 octets OD shall be used for Read access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x11;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength) = 0 c) MTU_DPP1_Set(OutputLength) = 0 d) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID e) MTU_ISDU_Add(Index = 24, Subindex = 0, Value = testdata);Test parameter f) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns "Reached" g) Evaluation 1) h) SMI_DeviceRead(ABPS_DEVICEREAD(Index = 24, Subindex = 0));returns "OD" i) Evaluation 2) j) MTU_State_CheckOperate;returns "MTU in OPERATE" k) Evaluation 3)

Test parameter testdata = "0x74, 0x65, 0x73, 0x74, 0x20, 0x6F, 0x66, 0x20, 0x74, 0x68, 0x65, 0x20, 0x6D, 0x61, 0x73, 0x74, 0x65, 0x72, 0x73, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x33, 0x32, 0x20, 0x6F, 0x64, 0x64, 0x61"

Post condition – 2143

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Reached"

2. Check "OD"

3. Check "MTU in OPERATE"

Test passed "Reached" = TRUE, and "OD" = testdata, and "MTU in OPERATE" = FALSE

Test not passed (examples) Any evaluation failed or communication error

Report All evaluations <ok | nok>

2144

2145

### Version V1.1.5 – 292 – Test Specification © IO-Link

### 2146 9.6.3 Master in PREOPERATE uses TYPE_1_V to read Index 24 (8 OD)

### 2147 Table 225 defines the test conditions for this test case.

### 2148 Table 225 – Master in PREOPERATE uses TYPE_1_V to read Index 24 (8 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0204

Name TCM_DLOD_PREP_TYPE1VREADOD8

Purpose (short) Master uses TYPE_1_V to read Index 24 in PREOPERATE (8 octets OD)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.3, A.5.2, B.2.1, Tables A.8, A.13

Configuration / setup Master-Tester-System 2149

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for PREOPERATE. TYPE_0_V with 8 octets OD shall be used for Read access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x21;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength) = 0 c) MTU_DPP1_Set(OutputLength) = 0 d) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID e) MTU_ISDU_Add(Index = 24, Subindex = 0, Value = testdata) f) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns "Reached" g) Evaluation 1) h) SMI_DeviceRead(ABPS_DEVICEREAD(Index = 24, Subindex = 0));returns "OD" i) Evaluation 2) j) MTU_State_CheckOperate;returns "MTU in OPERATE" k) Evaluation 3)

Test parameter testdata = "0x74, 0x65, 0x73, 0x74, 0x20, 0x6F, 0x66, 0x20, 0x74, 0x68, 0x65, 0x20, 0x6D, 0x61, 0x73, 0x74, 0x65, 0x72, 0x73, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x33, 0x32, 0x20, 0x6F, 0x64, 0x64, 0x61"

Post condition – 2150

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Reached"

2. Check "OD"

3. Check "MTU in OPERATE"

Test passed "Reached" = TRUE, and "OD" = testdata, and "MTU in OPERATE" = FALSE

Test not passed (examples) Any evaluation failed or communication error

Report All evaluations <ok | nok>

2151

2152

### Test Specification © IO-Link – 293 – Version V1.1.5

### 2153 9.6.4 Master in PREOPERATE uses TYPE_1_V to read Index 24 (32 OD)

### 2154 Table 226 defines the test conditions for this test case.

### 2155 Table 226 – Master in PREOPERATE uses TYPE_1_V to read Index 24 (32 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0205

Name TCM_DLOD_PREP_TYPE1VREADOD32

Purpose (short) Master uses TYPE_1_V to read Index 24 in PREOPERATE (32 octets OD)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.3, A.5.2, B.2.1, Tables A.8, A.13

Configuration / setup Master-Tester-System 2156

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for PREOPERATE. TYPE_1_V with 32 octets OD shall be used for Read access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x31;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength) = 0 c) MTU_DPP1_Set(OutputLength) = 0 d) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID e) MTU_ISDU_Add(Index = 24, Subindex = 0, Value = testdata) f) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns "Reached" g) Evaluation 1) h) SMI_DeviceRead(ABPS_DEVICEREAD(Index = 24, Subindex = 0));returns "OD" i) Evaluation 2) j) MTU_State_CheckOperate;returns "MTU in OPERATE" k) Evaluation 3)

Test parameter testdata = "0x74, 0x65, 0x73, 0x74, 0x20, 0x6F, 0x66, 0x20, 0x74, 0x68, 0x65, 0x20, 0x6D, 0x61, 0x73, 0x74, 0x65, 0x72, 0x73, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x33, 0x32, 0x20, 0x6F, 0x64, 0x64, 0x61"

Post condition – 2157

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Reached"

2. Check "OD"

2. Check "MTU in OPERATE"

Test passed "Reached" = TRUE, and "OD" = testdata, and "MTU in OPERATE" = FALSE

Test not passed (examples) Any evaluation failed or communication error

Report All evaluations <ok | nok>

2158

2159

### Version V1.1.5 – 294 – Test Specification © IO-Link

### 2160 9.6.5 Master in PREOPERATE uses TYPE_0 to write Index 24 (1 OD)

### 2161 Table 227 defines the test conditions for this test case.

### 2162 Table 227 – Master in PREOPERATE uses TYPE_0 to write Index 24 (1 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0206

Name TCM_DLOD_PREP_TYPE0WRITEOD1

Purpose (short) Master uses TYPE_0 to write to Index 24 in PREOPERATE (1 octet OD)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.2, A.5.2, B.2.1, Tables A.8, A.13

Configuration / setup Master-Tester-System 2163

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for PREOPERATE. TYPE_0 with 1 octet OD shall be used for Write access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x01;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength) = 0 c) MTU_DPP1_Set(OutputLength) = 0 d) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID e) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns "Reached" f) Evaluation 1) g) MTU_ISDU_Add(Index = 24, Subindex = 0, OD = [0x74]);preset data h) SMI_DeviceWrite(ABPS_DEVICEWRITE <Index=24, Subindex = 0, Value = [0x54]>);writes test data i) MTU_ISDU_Read(Index = 24, Subindex = 0);returns "OD" j) Evaluation 2) k) MTU_State_CheckOperate;returns "MTU in OPERATE" l) Evaluation 3)

Test parameter –

Post condition – 2164

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Reached"

2. Check OD

3. Check "MTU in OPERATE"

Test passed "Reached" = TRUE, and "OD" = [0x54], and "MTU in OPERATE" = FALSE

Test not passed (examples) Any evaluation failed or communication error

Report All evaluations <ok | nok>

2165

2166

### Test Specification © IO-Link – 295 – Version V1.1.5

### 2167 9.6.6 Master in PREOPERATE uses TYPE_1_2 to write Index 24 (2 OD)

### 2168 Table 228 defines the test conditions for this test case.

### 2169 Table 228 – Master in PREOPERATE uses TYPE_1_2 to write Index 24 (2 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0207

Name TCM_DLOD_PREP_TYPE12WRITEOD2

Purpose (short) Master uses TYPE_1_2 to write to Index 24 in PREOPERATE (2 octets OD)

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.3, A.5.2, B.2.1, Tables A.8, A.13

Configuration / setup Master-Tester-System 2170

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for PREOPERATE. TYPE_1_2 with 2 octets OD shall be used for Write access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x11;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength) = 0 c) MTU_DPP1_Set(OutputLength) = 0 d) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID e) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns "Reached" f) Evaluation 1) g) MTU_ISDU_Add(Index = 24, Subindex = 0, OD = [0x74, 0x75]);preset data h) SMI_DeviceWrite(ABPS_DEVICEWRITE <Index=24, Subindex = 0, Value = [0x54, 0x65]>);writes test data i) MTU_ISDU_Read(Index = 24, Subindex = 0);returns "OD" j) Evaluation 2) k) MTU_State_CheckOperate;returns "MTU in OPERATE" l) Evaluation 3)

Test parameter –

Post condition – 2171

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Reached"

2. Check OD

3. Check "MTU in OPERATE"

Test passed "Reached" = TRUE, and "OD" = [0x54, 0x65], and "MTU in OPERATE" = FALSE

Test not passed (examples) Any evaluation failed or communication error

Report All evaluations <ok | nok>

2172

2173

### Version V1.1.5 – 296 – Test Specification © IO-Link

### 2174 9.6.7 Master in PREOPERATE uses TYPE_1_V to write Index 24 (8 OD)

### 2175 Table 229 defines the test conditions for this test case.

### 2176 Table 229 – Master in PREOPERATE uses TYPE_1_V to write Index 24 (8 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0208

Name TCM_DLOD_PREP_TYPE1VWRITEOD8

Purpose (short) Master uses TYPE_1_V to write to Index 24 in PREOPERATE (8 octets OD)

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.3, A.5.2, B.2.1, Tables A.8, A.13

Configuration / setup Master-Tester-System 2177

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for PREOPERATE. TYPE_1_V with 8 octets OD shall be used for Write access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x21;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength) = 0 c) MTU_DPP1_Set(OutputLength) = 0 d) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID e) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns "Reached" f) Evaluation 1) g) MTU_ISDU_Add(Index = 24, Subindex = 0, OD = [0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B]);preset data h) SMI_DeviceWrite(ABPS_DEVICEWRITE <Index=24, Subindex = 0, Value = [0x54, 0x65, 0x73, 0x74, 0x20, 0x38, 0x4F, 0x44]>);writes test data i) MTU_ISDU_Read(Index = 24, Subindex = 0);returns "OD" j) Evaluation 2) k) MTU_State_CheckOperate;returns "MTU in OPERATE" l) Evaluation 3)

Test parameter –

Post condition – 2178

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Reached"

2. Check OD

3. Check "MTU in OPERATE"

Test passed "Reached" = TRUE, and "OD" = [0x54, 0x65, 0x73, 0x74, 0x20, 0x38, 0x4F, 0x44], and "MTU in OPERATE" = FALSE

Test not passed (examples) Any evaluation failed or communication error

Report All evaluations <ok | nok>

2179

2180

### Test Specification © IO-Link – 297 – Version V1.1.5

### 2181 9.6.8 Master in PREOPERATE uses TYPE_1_V to write Index 24 (32 OD)

### 2182 Table 230 defines the test conditions for this test case.

### 2183 Table 230 – Master in PREOPERATE TYPE_1_V to write Index 24 (32 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0209

Name TCM_DLOD_PREP_TYPE1VWRITEOD32

Purpose (short) Master uses TYPE_1_V to write to Index 24 in PREOPERATE (32 octets OD)

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.3, A.5.2, B.2.1, Tables A.8, A.13

Configuration / setup Master-Tester-System 2184

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for PREOPERATE. TYPE_1_V with 32 octets OD shall be used for Write access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedureo a) MTU_DPP1_Set(M-sequenceCapability) = 0x31;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength) = 0 c) MTU_DPP1_Set(OutputLength) = 0 d) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID e) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns "Reached" f) Evaluation 1) g) MTU_ISDU_Add(Index = 24, Subindex = 0, OD = [0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x91, 0x92, 0x93]);preset data h) SMI_DeviceWrite(ABPS_DEVICEWRITE <Index=24, Subindex = 0, Value = [0x54, 0x65, 0x73, 0x74, 0x20, 0x6F, 0x66, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4D, 0x61, 0x73, 0x74, 0x65, 0x72, 0x73, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x33, 0x32, 0x20, 0x4F, 0x44, 0x44, 0x41]>);writes test data i) MTU_ISDU_Read(Index = 24, Subindex = 0);returns "OD" j) Evaluation 2) k) MTU_State_CheckOperate;returns "MTU in OPERATE" l) Evaluation 3)

Test parameter –

Post condition – 2185

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Reached"

2. Check OD

2. Check "MTU in OPERATE"

Test passed "Reached" = TRUE, and "OD" = [0x54, 0x65, 0x73, 0x74, 0x20, 0x6F, 0x66, 0x20, 0x74, 0x68, 0x65, 0x20, 0x4D, 0x61, 0x73, 0x74, 0x65, 0x72, 0x73, 0x20, 0x77, 0x69, 0x74, 0x68, 0x20, 0x33, 0x32, 0x20, 0x4F, 0x44, 0x44, 0x41], and "MTU in OPERATE" = FALSE

Test not passed (examples) Any evaluation failed or communication error

Report All evaluations <ok | nok>

2186

2187

### Version V1.1.5 – 298 – Test Specification © IO-Link

### 2188 9.7 OPERATE

### 2189 9.7.1 Master in OPERATE uses TYPE_1_V for zero bit PD in/out (8 OD)

### 2190 Table 231 defines the test conditions for this test case.

### 2191 Table 231 – Master in OPERATE uses TYPE_1_V for zero bit PD in/out (8 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0336

Name TCM_ DLPD_CYCC_TYPE1V_OD8BIT0INBIT0OUT

Purpose (short) Master uses M-sequence TYPE_1_V for zero bit PD in/out (8 octets OD)

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.5, 11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (row 3)

Configuration / setup Master-Tester-System 2192

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Master reads OD length and PD input/output length during STARTUP through DPP1 from Device. Due to these values, the Master shall select M-sequence TYPE_1_V in OPERATE and provide this information to SMI services.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x1D;see B.1.4 in [1] b) MTU_DPP1_Set(PDIn length) = 0x00;"0" Bit c) MTU_DPP1_Set(PDOut length) = 0x00;"0" Bit d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);switch to OPERATE e) TM_AWAIT_PORT_STATUS(OPERATE) f) MTU_State_CheckOperate;returns "MTU in OPERATE" g) Evaluation 1) h) SMI_PDIn;returns "InputDataLength" i) Evaluation 2) j) SMI_PDInOut;returns "InputDataLength", "OutputDataLength" k) Evaluation 3) l) SMI_PortStatus;returns "RevisionID", "InputDataLength", "OutputDataLength" m) Evaluation 4) n) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>)

Test parameter –

Post condition – 2193

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check value of PDIn.InputDataLength

3. Check value of PDInOut.InputDataLength and PDInOut.OutputDataLength

4. Check value of PortStatus.RevisionID, PortStatus.InputDataLength, and

Portstatus.OutputDataLength

5. Check "vendorname"

Test passed "MTU in OPERATE" = TRUE, and PDIn: InputDataLength = "0", and PDInOut: InputDataLength = "0", OutputDataLength = "0", and PortStatus: RevisionID = 0x11, InputDataLength = "0", OutputDataLength = "0", and <vendorname> = "IO-Link Community"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report vendorname ok: <yes/no> <ok | nok>

2194

2195

### Test Specification © IO-Link – 299 – Version V1.1.5

### 2196 9.7.2 Master in OPERATE uses TYPE_1_V for zero bit PD in/out (32 OD)

### 2197 Table 232 defines the test conditions for this test case.

### 2198 Table 232 – Master in OPERATE uses TYPE_1_V for zero bit PD in/out (32 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0337

Name TCM_ DLPD_CYCC_TYPE1V_OD32BIT0INBIT0OUT

Purpose (short) Master uses M-sequence TYPE_1_V for zero bit PD in/out (32 octets OD)

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master protocol test: test to pass

Specification (clause) [1], 9.2.3.5, 11.2.7, 11.2.17, 11.2.18, 11.2.19, A.2.6, E.4, E.10, E.11, E.12, Table A.10 (row 4)

Configuration / setup Master-Tester-System 2199

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) The Master reads OD length and PD input/output length during STARTUP through DPP1 from Device. Due to these values, the Master shall select M-sequence TYPE_1_V in OPERATE and provide this information to SMI services.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x1F;see B.1.4 in [1] b) MTU_DPP1_Set(PDIn length) = 0x00;"0" Bit c) MTU_DPP1_Set(PDOut length) = 0x00;"0" Bit d) SMI_PortConfiguration (ABPS_NOTYPE_CHECK);switch to OPERATE e) TM_AWAIT_PORT_STATUS(OPERATE) f) MTU_State_CheckOperate;returns "MTU in OPERATE" g) Evaluation 1) h) SMI_PDIn;returns "InputDataLength" i) Evaluation 1) j) SMI_PDInOut;returns "InputDataLength", "OutputDataLength" k) Evaluation 2) l) SMI_PortStatus;returns "RevisionID", "InputDataLength", "OutputDataLength" m) Evaluation 3) n) SMI_DeviceRead(ABPS_DEVICEREAD <vendorname>)

Test parameter –

Post condition – 2200

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check value of PDIn.InputDataLength

3. Check value of PDInOut.InputDataLength and PDInOut.OutputDataLength

4. Check value of PortStatus.RevisionID, PortStatus.InputDataLength, and

Portstatus.OutputDataLength

5. Check "vendorname"

Test passed "MTU in OPERATE" = TRUE, and PDIn: InputDataLength = "0", and PDInOut: InputDataLength = "0", OutputDataLength = "0", and PortStatus: RevisionID = 0x11, InputDataLength = "0", OutputDataLength = "0", and <vendorname> = "IO-Link Community"

Test not passed (examples) Any of the checks in Test passed failed or communication error

Report vendorname ok: <yes/no> <ok | nok>

2201

2202

### Version V1.1.5 – 300 – Test Specification © IO-Link

### 2203 9.7.3 Master in OPERATE uses TYPE_0 to read Index 24 (1 OD)

### 2204 Table 233 defines the test conditions for this test case.

### 2205 Table 233 – Master in OPERATE uses TYPE_0 to read Index 24 (1 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0210

Name TCM_DLOD_OPER_TYPE0READOD1

Purpose (short) Master uses TYPE_0 to read Index 24 in OPERATE (1 octet OD)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.2, A.5.2, B.2.1, Tables A.10, A.13

Configuration / setup Master-Tester-System 2206

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for OP- ERATE. TYPE_0 with 1 octet OD shall be used for Read access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x01;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength = 0) c) MTU_DPP1_Set(OutputLength = 0) d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) e) MTU_ISDU_Add(Index = 24, Subindex = 0, OD = [0x74]);test data f) TM_AWAIT_PORT_STATUS(OPERATE) g) MTU_State_CheckOperate;returns "MTU in OPERATE" h) Evaluation 1) i) SMI_DeviceRead(ABPS_DEVICEREAD(Index = 24, Subindex = 0);returns "OD" j) Evaluation 2)

Test parameter –

Post condition – 2207

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "MTU in OPERATE"

2. Check "OD"

Test passed "MTU in OPERATE" = TRUE "OD" = [0x74]

Test not passed (examples) Any evaluation failed or communication eror

Report All evaluations <ok | nok>

2208

2209

### Test Specification © IO-Link – 301 – Version V1.1.5

### 2210 9.7.4 Master in OPERATE uses TYPE_0 to write Index 24 (1 OD)

### 2211 Table 234 defines the test conditions for this test case.

### 2212 Table 234 – Master in OPERATE uses TYPE_0 to write Index 24 (1 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0211

Name TCM_DLOD_OPER_TYPE0WRITEOD1

Purpose (short) Master uses TYPE_0 to write to Index 24 in OPERATE (1 octet OD)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.2, A.5.2, B.2.1, Tables A.10, A.13

Configuration / setup Master-Tester-System 2213

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for OP- ERATE. TYPE_0 with 1 octet OD shall be used for Write access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x01;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength = 0) c) MTU_DPP1_Set(OutputLength = 0) d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) e) MTU_ISDU_Add(Index = 24, Subindex = 0, OD = [0x74]);preset test data f) TM_AWAIT_PORT_STATUS(OPERATE) g) MTU_State_CheckOperate;returns "MTU in OPERATE" h) Evaluation 1) i) SMI_DeviceWrite(ABPS_DEVICEWRITE<Index=24, Subindex=0, Value=[0x54]>);write test data j) MTU_ISDU_Read(Index = 24, Subindex = 0);returns "OD" k) Evaluation 2)

Test parameter –

Post condition – 2214

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "MTU in OPERATE"

2. Check "OD"

Test passed "MTU in OPERATE" = TRUE "OD" = [0x54]

Test not passed (examples) Any evaluation failed or communication eror

Report All evaluations <ok | nok>

2215

2216

### Version V1.1.5 – 302 – Test Specification © IO-Link

### 2217 9.7.5 Master in OPERATE uses TYPE_1_2 to write Index 24 (2 OD)

### 2218 Table 235 defines the test conditions for this test case.

### 2219 Table 235 – Master in OPERATE uses TYPE_1_2 to write Index 24 (2 OD)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0212

Name TCM_DLOD_OPER_TYPE12WRITEOD2

Purpose (short) Master uses TYPE_1_2 to write to Index 24 in OPERATE (2 octets OD)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master protocol test: test to pass

Specification (clause) [1], A.2.3, A.5.2, B.2.1, Tables A.10, A.13

Configuration / setup Master-Tester-System 2220

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master uses correct M_sequence type according to Device settings in DPP1 for OP- ERATE. TYPE_1_2 with 2 octet OD shall be used for Write access.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x11;see B.1.4 in [1] b) MTU_DPP1_Set(InputLength = 0) c) MTU_DPP1_Set(OutputLength = 0) d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) e) MTU_ISDU_Add(Index = 24, Subindex = 0, OD = [0x74]);preset test data f) TM_AWAIT_PORT_STATUS(OPERATE) g) MTU_State_CheckOperate;returns "MTU in OPERATE" h) Evaluation 1) i) SMI_DeviceWrite(ABPS_DEVICEWRITE <Index=24, Subindex=0, Value=[0x54, 0x65]>);write test data j) MTU_ISDU_Read(Index = 24, Subindex = 0);returns "OD" k) Evaluation 2)

Test parameter –

Post condition – 2221

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "MTU in OPERATE"

2. Check "OD"

Test passed "MTU in OPERATE" = TRUE "OD" = [0x54, 0x65]

Test not passed (examples) Any evaluation failed or communication eror

Report All evaluations <ok | nok>

2222

2223

### Test Specification © IO-Link – 303 – Version V1.1.5

### 2224 9.8 Fallback

### 2225 9.8.1 Fallback from PREOPERATE

### 2226 Table 236 defines the test conditions for this test case.

### 2227 Table 236 – Fallback from PREOPERATE

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0213

Name TCM_DLFB_PROP_OK

Purpose (short) Master Fallback from PREOPERATE

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to pass

Specification (clause) [1], 7.3.2.3, Figure 34, 11.3.2, Figure 101, 9.2.1, Figure 69, 9.2.2.2, 5.2.2.1, Table B.2

Configuration / setup Master-Tester-System 2228

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Transition from PREOPERATE to SIO mode of Master Port via reconfiguration and check of its correct behavior afterwards. That means, Master communicates with Device and receives only a switching signal (DI) after reconfiguration. Hint: Master reconfiguration with a different VendorID than permitted for the Device causes Master to hold on in PREOPERATE.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID=1>);invalid VendorID b) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns ArgBlock "PortStatusList" c) Evaluation 1) d) MTU_State_CheckPreoperate;returns "MTU in PREOPERATE" e) Evaluation 2) f) SMI_PortConfiguration(ABPS_PORTTODI);switch to DI mode g) MTU_State_CheckFallback;returns "Master sent Fallback" h) Evaluation 3) i) TM_AWAIT_DI_HIGH;PDIn.PDI0="1" within MTU_Timeout j) Evaluation 4)

Test parameter –

Post condition – 2229

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check ArgBlock "PortStatusList"

2. Check "MTU in PREOPERATE"

3. Check "Master sent Fallback"

4. Check MTU_Timeout

Test passed "PortStatusList.PortStatusInfo" = PORT_DIAG, and "MTU in PREOPERATE" = TRUE, and "Master sent Fallback" = TRUE, and No MTU_Timeout

Test not passed (examples) Any evaluation failed or MTU_Timeout

Report All evaluations: <ok | nok>

2230

2231

### Version V1.1.5 – 304 – Test Specification © IO-Link

### 2232 9.8.2 Fallback from PREOPERATE fails

### 2233 Table 237 defines the test conditions for this test case.

### 2234 Table 237 – Fallback from PREOPERATE fails

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0214

Name TCM_DLFB_PROP_FAILS

Purpose (short) Master Fallback from PREOPERATE fails

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to pass

Specification (clause) [1], 7.3.2.3, 7.3.2.4, Figure 34, Table B.2

Configuration / setup Master-Tester-System 2235

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Transition from PREOPERATE to SIO mode of Master Port via reconfiguration and check of its correct behavior afterwards. It is assumed that reconfiguration fails and Master iniates a Port restart or stops communicating. Hint: Master reconfiguration with a different VendorID than permitted for the Device causes Master to hold on in PREOPERATE.

Precondition EUT: PORT_INACTIVE MTU: MTU_STANDARD_STATE

Procedure a) MTU_DPP1_Set(M-sequenceCapability) = 0x21;see B.1.4 in [1] b) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID c) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns ArgBlock "PortStatusList" d) Evaluation 1) e) MTU_State_CheckPreoperate;returns "MTU in PREOPERATE" f) Evaluation 2) g) MTU_State_BlockFallback(n=2);2 × refuse Fallback acknowledge h) SMI_PortConfiguration(ABPS_PORTTODI);switch to DI mode i) MTU_State_CheckFallback;returns "Master sent Fallback" j) Evaluation 3) k) TM_AWAIT_DI_HIGH;PDIn.PDI0="1" within Test_Timeout l) Evaluation 4)

Test parameter –

Post condition – 2236

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check ArgBlock "PortStatusList"

2. Check "MTU in PREOPERATE"

3. Check "Master sent Fallback"

4. Check Test_Timeout

Test passed "PortStatusList.PortStatusInfo" = PORT_DIAG, and "MTU in PREOPERATE" = TRUE, and "Master sent Fallback" = TRUE, and No Test_Timeout

Test not passed (examples) Any evaluation failed

Report All evaluations: <ok | nok>

2237

2238

### Test Specification © IO-Link – 305 – Version V1.1.5

### 2239 9.8.3 Fallback from OPERATE

### 2240 Table 238 defines the test conditions for this test case.

### 2241 Table 238 – Fallback from OPERATE

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0215

Name TCM_DLFB_OPER_OK

Purpose (short) Master Fallback from OPERATE

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to pass

Specification (clause) [1], 7.3.2.3, 7.3.2.4, Figure 34, Table B.2

Configuration / setup Master-Tester-System 2242

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Transition from OPERATE to SIO mode of Master Port via reconfiguration and check of its correct behavior afterwards. That means, Master communicates with Device and receives only a switching signal (DI) after reconfiguration.

Precondition EUT: PORT_AUTOSTART MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) SMI_PortConfiguration(ABPS_PORTTODI);switch to DI mode d) MTU_State_CheckFallback;returns "Master sent Fallback" e) Evaluation 2) f) TM_AWAIT_DI_HIGH;PDIn.PDI0 = 1 within MTU_Timeout g) Evaluation 3)

Test parameter –

Post condition – 2243

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "MTU in OPERATE"

2. Check "Master sent Fallback"

3. Check MTU_Timeout

Test passed "MTU in OPERATE" = TRUE, and "Master sent Fallback" = TRUE, and No MTU_Timeout

Test not passed (examples) Any evaluation failed or MTU_Timeout

Report All evaluations: <ok | nok>

2244

2245

### Version V1.1.5 – 306 – Test Specification © IO-Link

### 2246 9.8.4 Fallback from OPERATE fails

### 2247 Table 239 defines the test conditions for this test case.

### 2248 Table 239 – Fallback from OPERATE fails

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0216

Name TCM_DLFB_OPER_FAILS

Purpose (short) Master Fallback from OPERATE fails

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to pass

Specification (clause) [1], 7.3.2.3, 7.3.2.4, Figure 34, Table B.2

Configuration / setup Master-Tester-System 2249

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Transition from OPERATE to SIO mode of Master Port via reconfiguration and check of its correct behavior afterwards. That means, Master shall send two times the Fallback command after the reception of a second invalid response. Upon receipt of the invalid response, Master iniates a Port restart or stops communicating.

Precondition EUT: PORT_AUTOSTART MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) MTU_State_BlockFallback(n=2);2 × refuse Fallback acknowledge d) SMI_PortConfiguration(ABPS_PORTTODI) e) MTU_State_CheckFallback;returns "Master sent Fallback" f) Evaluation 2) g) TM_AWAIT_DI_HIGH;PDIn.PDI0="1" within Test__Timeout h) Evaluation 3)

Test parameter –

Post condition – 2250

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "MTU in OPERATE"

2. Check "Master sent Fallback"

3. Check Test_Timeout

Test passed "MTU in OPERATE" = TRUE, and "Master sent Fallback" = TRUE, and No Test_Timeout

Test not passed (examples) Any evaluation failed

Report All evaluations: <ok | nok>

2251

2252

### Test Specification © IO-Link – 307 – Version V1.1.5

### 2253 9.9 Retry

### 2254 9.9.1 Retries after 2 reply messages with incorrect Checksum (STARTUP)

### 2255 Table 240 defines the test conditions for this test case.

### 2256 Table 240 – Retries after 2 reply messages with incorrect Checksum (STARTUP)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0338

Name TCM_DLCC_RTRY_CKSINCORR2STRTUP

Purpose (short) Retry behavior after 2 reply messages with incorrect checksum in STARTUP

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to pass

Specification (clause) [1], 7.3.3.4, Figure 40, Table 46

Configuration / setup Master-Tester-System 2257

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving reply message with incorrect checksum at STARTUP. Master repeats the request message and again reply with incorrect checksum. After a third trial, reply is correct.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) MTU_State_IncorrectChecksums(n=2);2 x reply with incorrect checksum c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);begin with start-up d) TM_AWAIT_PORT_STATUS(OPERATE) e) MTU_State_CountRestarts;returns "Restarts" f) Evaluation 1)

Test parameter –

Post condition – 2258

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Restarts"

Test passed "Restarts" = 1

Test not passed (examples) Any evaluation failed or communication error or Procedure does not terminate

Report All evaluations <ok | nok>

2259

2260

### Version V1.1.5 – 308 – Test Specification © IO-Link

### 2261 9.9.2 Retries after 2 reply messages with incorrect Checksum (PREOP)

### 2262 Table 241 defines the test conditions for this test case.

### 2263 Table 241 – Retries after 2 reply messages with incorrect Checksum (PREOP)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0339

Name TCM_DLCC_RTRY_ CKSINCORR2PREOP

Purpose (short) Retry behavior after 2 reply messages with incorrect checksum in PREOPERATE

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to pass

Specification (clause) [1], 7.3.3.4, Figure 40, Table 46

Configuration / setup Master-Tester-System 2264

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving reply message with incorrect checksum at PREOPERATE. Master repeats the request message and again reply with incorrect checksum. After a third trial, reply is correct.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID c) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns ArgBlock "PortStatusList" d) Evaluation 1) ef) MTU_State_IncorrectChecksum(n=2);2 x reply with incorrect checksum f) SMI_DeviceRead(ABPS_DEVICEREAD [Index=0x10]);returns "VendorName" g) Evaluation 2) h) MTU_State_CheckPreoperate;returns "Reached" i) Evaluation 3) j) MTU_State_CountRestarts;returns "Restarts" k) Evaluation 4)

Test parameter –

Post condition – 2265

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check ArgBlock "PortStatusList"

2. Check "VendorName"

3. Check "Reached"

4. Check "Restarts"

Test passed "PortStatusList.PortStatusInfo" = PORT_DIAG, and "VendorName" = IO-Link Community, and "Reached" = TRUE, and "Restarts" = 1

Test not passed (examples) Any evaluation failed, or SMI_DeviceRead with negative response, or Procedure does not terminate

Report All evaluations <ok | nok>

2266

2267

### Test Specification © IO-Link – 309 – Version V1.1.5

### 2268 9.9.3 Retries after 2 reply messages with incorrect Checksum (OPERATE)

### 2269 Table 242 defines the test conditions for this test case.

### 2270 Table 242 – Retries after 2 reply messages with incorrect Checksum (OPERATE)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0340

Name TCM_DLCC_RTRY_ CKSINCORR2OPERATE

Purpose (short) Retry behavior after 2 reply messages with incorrect checksum in OPERATE

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to pass

Specification (clause) [1], 7.3.3.4, Figure 40, Table 46

Configuration / setup Master-Tester-System 2271

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving reply message with incorrect checksum at OPERATE. Master repeats the request message and again reply with incorrect checksum. After a third trial, reply is correct.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);begin with start-up c) TM_AWAIT_PORT_STATUS(OPERATE);returns ArgBlock "PortStatusList" d) Evaluation 1) e) MTU_State_IncorrectChecksum(n=2);2 x reply with incorrect checksum f) SMI_DeviceRead(ABPS_DEVICEREAD [Index=0x10]);returns "VendorName" g) Evaluation 2) h) MTU_State_CountRestarts;returns "Restarts" i) Evaluation 3)

Test parameter –

Post condition – 2272

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check ArgBlock "PortStatusList"

2. Check "VendorName"

3. Check "Restarts"

Test passed "PortStatusList.PortStatusInfo" = PORT_DIAG, and "VendorName" = IO-Link Community, and "Restarts" = 1

Test not passed (examples) Any evaluation failed, or SMI_DeviceRead with negative response, or Procedure does not terminate

Report All evaluations <ok | nok>

2273

2274

### Version V1.1.5 – 310 – Test Specification © IO-Link

### 2275 9.9.4 Retries after 3 reply messages with incorrect Checksum (STARTUP)

### 2276 Table 243 defines the test conditions for this test case.

### 2277 Table 243 – Retries after 3 reply messages with incorrect Checksum (STARTUP)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0341

Name TCM_DLCC_RTRY_CKSINCORR3STRUP&RESTOP

Purpose (short) Retry behavior after 3 reply messages with incorrect CKS at STARTUP (restart/stop)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to fail

Specification (clause) [1], 7.3.3.4, Figure 40, Table 46

Configuration / setup Master-Tester-System 2278

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving reply message with incorrect checksum at STARTUP. Master repeats the request message and again reply with incorrect checksum. After a third trial, reply is still incorrect.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) MTU_State_IncorrectChecksum(n=3);3 x reply with incorrect checksum c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);begin with start-up d) TM_AWAIT_PORT_STATUS (OPERATE) e) MTU_State_CountRestarts;returns "Restarts" f) Evaluation 1)

Test parameter –

Post condition – 2279

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Restarts"

Test passed "Restarts" = 2

Test not passed (examples) Any evaluation failed, or Procedure does not terminate

Report All evaluations <ok | nok>

2280

2281

### Test Specification © IO-Link – 311 – Version V1.1.5

### 2282 9.9.5 Retries after 3 reply messages with incorrect Checksum (PREOP)

### 2283 Table 244 defines the test conditions for this test case.

### 2284 Table 244 – Retries after 3 reply messages with incorrect Checksum (PREOP)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0342

Name TCM_DLCC_RTRY_CKSINCORR3PREOP&RESTOP

Purpose (short) Retry behavior after 3 reply messages with incorrect CKS at PREOP (restart/stop)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to fail

Specification (clause) [1], 7.3.3.4, Figure 40, Table 46

Configuration / setup Master-Tester-System 2285

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving reply message with incorrect checksum at PREOPERATE. Master repeats the request message and again reply with incorrect checksum. After a third trial, reply is still incorrect.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID c) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns ArgBlock "PortStatusList" d) Evaluation 1) e) MTU_State_CheckPreoperate;returns "reached" f) Evaluation 2) g) MTU_State_IncorrectChecksum(n=3);3 x reply with incorrect checksum h) SMI_DeviceRead(ABPS_DEVICEREAD [Index=0x10]);dummy read for delay i) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns ArgBlock "PortStatusList" j) Evaluation 3) k) MTU_State_CheckPreoperate;returns "reached" l) Evaluation 4) m) MTU_State_CountRestarts;returns "Restarts" n) Evaluation 5)

Test parameter –

Post condition – 2286

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check ArgBlock "PortStatusList"

2. Check "reached"

3. Check ArgBlock "PortStatusList"

4. Check "reached"

5. Check "Restarts"

Test passed "PortStatusList.PortStatusInfo" = PORT_DIAG, and;PREOPERATE reached "reached" = TRUE, and "PortStatusList.PortStatusInfo" = PORT_DIAG, and;PREOPERATE reached "reached" = TRUE, and "RESTARTS" = 2

Test not passed (examples) Any evaluation failed, or Procedure does not terminate

Report All evaluations <ok | nok>

2287

2288

### Version V1.1.5 – 312 – Test Specification © IO-Link

### 2289 9.9.6 Retries after 3 reply messages with incorrect Checksum (OPERATE)

### 2290 Table 245 defines the test conditions for this test case.

### 2291 Table 245 – Retries after 3 reply messages with incorrect Checksum (OPERATE)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0343

Name TCM_DLCC_RTRY_CKSINCORR3OPER&RESTOP

Purpose (short) Retry behavior after 3 reply messages with incorrect CKS at OPERATE (restart/stop)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to fail

Specification (clause) [1], 7.3.3.4, Figure 40, Table 46

Configuration / setup Master-Tester-System 2292

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving reply message with incorrect checksum at OPERATE. Master repeats the request message and again reply with incorrect checksum. After a third trial, reply is still incorrect.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);begin with start-up c) TM_AWAIT_PORT_STATUS(OPERATE);returns ArgBlock "PortStatusList" d) Evaluation 1) e) MTU_State_CheckPreoperate;returns "reached" f) Evaluation 2) g) MTU_State_IncorrectChecksum(n=3);3 x reply with incorrect checksum h) SMI_DeviceRead(ABPS_DEVICEREAD [Index=0x10]);dummy read for delay i) TM_AWAIT_PORT_STATUS(OPERATE);returns ArgBlock "PortStatusList" j) Evaluation 4) k) MTU_State_CheckPreoperate;returns "reached" l) Evaluation 5) m) MTU_State_CountRestarts;returns "Restarts" n) Evaluation 6)

Test parameter –

Post condition – 2293

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check ArgBlock "PortStatusList"

2. Check "reached"

3. Check ArgBlock "PortStatusList"

4. Check "reached"

5. Check "Restarts"

Test passed "PortStatusList.PortStatusInfo" = OPERATE, and "reached" = TRUE, and "PortStatusList.PortStatusInfo" = OPERATE, and "reached" = TRUE, and "RESTARTS" = 2

Test not passed (examples) Any evaluation failed, or Procedure does not terminate

Report All evaluations <ok | nok>

2294

2295

### Test Specification © IO-Link – 313 – Version V1.1.5

### 2296 9.9.7 Retries after 2 missing replies and finally correct Checksum (STARTUP)

### 2297 Table 246 defines the test conditions for this test case.

### 2298 Table 246 – Retries after 2 missing replies and finally correct Checksum (STARTUP)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0344

Name TCM_DLCC_RTRY_MISSREP2STRTUP

Purpose (short) Retry behavior after 2 missing replies and finally correct Checksum in STARTUP

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to pass

Specification (clause) [1], 7.3.3.4, Figure 40, Table 46

Configuration / setup Master-Tester-System 2299

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving no reply messages at STARTUP. Master sends request message, MTU does not send reply message. Master sends message again, MTU again does not send reply message. Master repeats the message once more and receives a valid reply message from MTU.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) MTU_State_SkipResponse (n=2);2 x no reply message c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);begin with start-up d) TM_AWAIT_PORT_STATUS (OPERATE) e) MTU_State_CountRestarts;returns "Restarts" f) Evaluation 1)

Test parameter –

Post condition – 2300

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Restarts"

Test passed "Restarts" = 1

Test not passed (examples) Any evaluation failed, or Communication error, or Procedure does not terminate

Report All evaluations <ok | nok>

2301

2302

### Version V1.1.5 – 314 – Test Specification © IO-Link

### 2303 9.9.8 Retries after 2 missing replies and finally correct Checksum (PREOP)

### 2304 Table 247 defines the test conditions for this test case.

### 2305 Table 247 – Retries after 2 missing replies and finally correct Checksum (PREOP)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0345

Name TCM_DLCC_RTRY_MISSREP2PREOP

Purpose (short) Retry behavior after 2 missing replies and finally correct Checksum in PREOP

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to pass

Specification (clause) [1], 7.3.3.4, Figure 40, Table 46

Configuration / setup Master-Tester-System 2306

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving no reply messages at PREOPERATE. Master sends request message, MTU does not send reply message. Master sends message again, MTU again does not send reply message. Master repeats the message once more and receives a valid reply message from MTU.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID c) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns ArgBlock "PortStatusList" d) Evaluation 1) e) MTU_State_CheckPreoperate;returns "reached" f) Evaluation 2) g) MTU_State_SkipResponse (n=2);2 x no reply message h) SMI_DeviceRead(ABPS_DEVICEREAD [Index=0x10]); dummy read for delay i) MTU_State_CheckPreoperate";returns "reached" j) Evaluation 3) k) MTU_State_CountRestarts;returns "Restarts" l) Evaluation 4)

Test parameter –

Post condition – 2307

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check ArgBlock "PortStatusList"

2. Check "reached"

3. Check "reached"

4. Check "Restarts"

Test passed "PortStatusList.PortStatusInfo" = PORT_DIAG, and;PREOPERATE reached "reached" = TRUE, and "reached" = TRUE, and "Restarts" = 1

Test not passed (examples) Any evaluation failed, or SMI_DeviceRead with negative response, or Procedure does not terminate

Report All evaluations <ok | nok>

2308

2309

### Test Specification © IO-Link – 315 – Version V1.1.5

### 2310 9.9.9 Retries after 2 missing replies and finally correct Checksum (OPERATE)

### 2311 Table 248 defines the test conditions for this test case.

### 2312 Table 248 – Retries after 2 missing replies and finally correct Checksum (OPERATE)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0346

Name TCM_DLCC_RTRY_MISSREP2OPER

Purpose (short) Retry behavior after 2 missing replies and finally correct Checksum in OPERATE

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to pass

Specification (clause) [1], 7.3.3.4, 9.3.3.2, Figure 40, Table 46

Configuration / setup Master-Tester-System 2313

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving no reply messages at OPERATE. Master sends request message, MTU does not send reply message. Master sends message again, MTU again does not send reply message. Master repeats the message once more and receives a valid reply message from MTU.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);begin with start-up c) TM_AWAIT_PORT_STATUS(OPERATE);returns ArgBlock "PortStatusList" d) Evaluation 1) e) MTU_State_CheckPreoperate;returns "reached" f) Evaluation 2) g) MTU_State_SkipResponse (n=2);2 x no reply message h) SMI_DeviceRead(ABPS_DEVICEREAD [Index=0x10]);returns "VendorName" i) Evaluation 3) j) MTU_State_CountRestarts;returns "Restarts" k) Evaluation 4)

Test parameter –

Post condition – 2314

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check ArgBlock "PortStatusList"

2. Check "reached"

3. Check "VendorName"

4. Check "Restarts"

Test passed "PortStatusList.PortStatusInfo" = OPERATE, and "reached" = TRUE, and "VendorName" = IO-Link Community, and "Restarts" = 1

Test not passed (examples) Any evaluation failed or SMI_DeviceRead with negative response Procedure does not terminate

Report All evaluations <ok | nok>

2315

2316

### Version V1.1.5 – 316 – Test Specification © IO-Link

### 2317 9.9.10 Retries after 3 missing replies ending with restart/stop (STARTUP)

### 2318 Table 249 defines the test conditions for this test case.

### 2319 Table 249 – Retries after 3 missing replies ending with restart/stop (STARTUP)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0347

Name TCM_DLCC_RTRY_REPMISS3STRTUP&RESTOP

Purpose (short) Retry behavior after 3 missing reply messages at STARTUP (restart/stop)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to fail

Specification (clause) [1], 7.3.3.4, 9.3.3.2, Figure 40, Table 46

Configuration / setup Master-Tester-System 2320

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving no reply messages at STARTUP. Master sends request message, MTU does not send reply message. Master sends message again, MTU again does not send reply message. After a third trial, reply is still missing.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) MTU_State_SkipResponse (n=3);3 x no reply message c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);begin with start-up d) TM_AWAIT_PORT_STATUS (OPERATE) e) MTU_State_CountRestarts;returns "Restarts" f) Evaluation 1)

Test parameter –

Post condition – 2321

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check "Restarts"

Test passed "Restarts" = 2

Test not passed (examples) Any evaluation failed, or Procedure does not terminate

Report All evaluations <ok | nok>

2322

2323

### Test Specification © IO-Link – 317 – Version V1.1.5

### 2324 9.9.11 Retries after 3 missing replies ending with restart/stop (PREOP)

### 2325 Table 250 defines the test conditions for this test case.

### 2326 Table 250 – Retries after 3 missing replies ending with restart/stop (PREOP)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0348

Name TCM_DLCC_RTRY_REPMISS3PREOP&RESTOP

Purpose (short) Retry behavior after 3 missing reply messages at PREOP (restart/stop)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to fail

Specification (clause) [1], 7.3.3.4, 9.3.3.2, Figure 40, Table 46

Configuration / setup Master-Tester-System 2327

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving no reply messages at PREOPERATE. Master sends request message, MTU does not send reply message. Master sends message again, MTU again does not send reply message. After a third trial, reply is still missing.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID c) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns ArgBlock "PortStatusList" d) Evaluation 1) e) MTU_State_CheckPreoperate;returns "reached" f) Evaluation 2) g) MTU_State_SkipResponse (n=3);3 x no reply message h) SMI_DeviceRead(ABPS_DEVICEREAD [Index=0x10]);dummy read for delay i) TM_AWAIT_PORT_STATUS(PORT_DIAG);returns ArgBlock "PortStatusList" j) Evaluation 3) k) MTU_State_CheckPreoperate;returns "reached" l) Evaluation 4) m) MTU_State_CountRestarts;returns "Restarts" n) Evaluation 5)

Test parameter –

Post condition – 2328

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check ArgBlock "PortStatusList"

2. Check "reached"

3. Check ArgBlock "PortStatusList"

4. Check "reached"

5. Check "Restarts"

Test passed "PortStatusList.PortStatusInfo" = PORT_DIAG, and;PREOPERATE reached "reached" = TRUE, and "PortStatusList.PortStatusInfo" = PORT_DIAG, and;PREOPERATE reached "reached" = TRUE, and "RESTARTS" = 2

Test not passed (examples) Any evaluation failed, or Procedure does not terminate

Report All evaluations <ok | nok>

2329

2330

### Version V1.1.5 – 318 – Test Specification © IO-Link

### 2331 9.9.12 Retries after 3 missing replies ending with restart/stop (OPERATE)

### 2332 Table 251 defines the test conditions for this test case.

### 2333 Table 251 – Retries after 3 missing replies ending with restart/stop (OPERATE)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0349

Name TCM_DLCC_RTRY_REPMISS3OPER&RESTOP

Purpose (short) Retry behavior after 3 missing reply messages at OPERATE (restart/stop)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master robustness test: test to fail

Specification (clause) [1], 7.3.3.4, 9.3.3.2, Figure 40, Table 46

Configuration / setup Master-Tester-System 2334

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Retry behavior of Master after receiving no reply messages at OPERATE. Master sends request message, MTU does not send reply message. Master sends message again, MTU again does not send reply message. After a third trial, reply is still missing.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);begin with start-up c) TM_AWAIT_PORT_STATUS(OPERATE);returns ArgBlock "PortStatusList" d) Evaluation 1) e) MTU_State_CheckPreoperate;returns "reached" f) Evaluation 2) g) MTU_State_SkipResponse (n=3);3 x no reply message h) SMI_DeviceRead(ABPS_DEVICEREAD [Index=0x10]);dummy read for delay i) TM_AWAIT_PORT_STATUS(OPERATE);returns ArgBlock "PortStatusList" j) Evaluation 3) k) MTU_State_CheckPreoperate;returns "reached" l) Evaluation 4) m) MTU_State_CountRestarts;returns "Restarts" n) Evaluation 5)

Test parameter –

Post condition – 2335

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check ArgBlock "PortStatusList"

2. Check "reached"

3. Check ArgBlock "PortStatusList"

4. Check "reached"

5. Check "Restarts"

Test passed "PortStatusList.PortStatusInfo" = OPERATE, and "reached" = TRUE, and "PortStatusList.PortStatusInfo" = OPERATE, and "reached" = TRUE, and "RESTARTS" = 2

Test not passed (examples) Any evaluation failed, or Procedure does not terminate

Report All evaluations <ok | nok>

2336

2337

### Test Specification © IO-Link – 319 – Version V1.1.5

### 2338 9.9.13 Maximum WURQs and no final success

### 2339 Table 252 defines the test conditions for this test case.

### 2340 Table 252 – Maximum WURQs and no final success

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0222

Name TCM_DLCC_RTRY_MAXWURQNOSUCCESS

Purpose (short) Behavior at maximum WURQ sequences and no final success

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master robustness test: test to fail

Specification (clause) [1], 7.3.2.2, 7.3.2.4, Figures 31, 32, and 33

Configuration / setup Master-Tester-System 2341

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master behavior with maximum WURQ sequences and no final success. Master is in STARTUP mode. Master sends at first a Wakeup pulse (WURQ) with subsequent communication requests. MTU does not respond to the subsequent communication requests. Master resends the Wakeup pulse (WURQ) with subsequent communication requests and again there is no response from MTU. Master repeats again the Wakeup pulse (WURQ) with subsequent communication requests and again there is no response from MTU. After a break, Master restarts the Wake-up requests and this time MTU responds in a correct manner.

Precondition EUT: PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_State_CountRestarts;reset counter b) MTU_State_SkipResponse (n = 3, WURQ=TRUE);3 x no reply message );9 x no reply message c) SMI_PortConfiguration (ABPS_NOTYPE_CHECK);begin with start-up d) TM_AWAIT_PORT_STATUS(OPERATE) e) MTU_State_CheckOperate;returns "reached" f) Evaluations 1) g) MTU_State_CountRestarts;returns "Restarts" h) Evaluation 2)

Test parameter -

Post condition - 2342

TEST CASE RESULTS CHECK / REACTION

Evaluations 1) Check “reached"

2. Check "Restarts"

Test passed "reached" = TRUE, and "Restarts" = 4;4 restarts counted, last with 1 s delay

Test not passed (examples) Any evaluation failed, or Communication error

Report All evaluations <ok | nok>

2343

2344

### Version V1.1.5 – 320 – Test Specification © IO-Link

### 2345 9.10 ISDU (Indexed Service Data Unit) – Application ErrorTypes

### 2346 9.10.1 ISDU Write rejected with ErrorType

### 2347 Table 253 defines the test conditions for this test case.

### 2348 Table 253 – ISDU Write rejected with ErrorType

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0223

Name TCM_ALIC_AERR_WRITEREJECT

Purpose (short) ISDU Write service rejected with defined ErrorType, no details

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2349

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write service rejected with ErrorType, no details. Access to a supported Index of the MTU is rejected with an application error without details. The response reports an ErrorCode "0x8000" (APP_DEV).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16383,Subindex=0,Value=[0x00],ErrorType_W=0x8000) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16383,Subindex=0,OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2350

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8000

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2351

2352

### Test Specification © IO-Link – 321 – Version V1.1.5

### 2353 9.10.2 ISDU Write to unsupported Index rejected with ErrorType

### 2354 Table 254 defines the test conditions for this test case.

### 2355 Table 254 – ISDU Write to unsupported Index rejected with ErrorType

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0224

Name TCM_ALIC_AERR_WRITEINDEXUNSUPPORTED

Purpose (short) ISDU Write to unsupported Index rejected with ErrorType

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2356

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write to unsupported Index rejected with ErrorType. Access to a non-supported Index in the MTU is rejected with an application error. The response reports an ErrorCode "0x8011" (IDX_NOTAVAIL).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=254, Subindex=0, Value=[0x00], ErrorType_W=0x8011) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=254, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2357

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8011

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2358

2359

### Version V1.1.5 – 322 – Test Specification © IO-Link

### 2360 9.10.3 ISDU Write to unsupported Subindex rejected with ErrorType

### 2361 Table 255 defines the test conditions for this test case.

### 2362 Table 255 – ISDU Write to unsupported Subindex rejected with ErrorType

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0225

Name TCM_ALIC_AERR_WRITESUBINDEXNOTSUPPORTED

Purpose (short) ISDU Write to unsupported Subindex (>0) rejected with ErrorType

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2363

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write to unsupported Subindex (>0) rejected with ErrorType. Access to a nonsupported Subindex in the MTU is rejected with an application error. The response reports an ErrorCode "0x8012" (SUBIDX_NOTAVAIL).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=253, Subindex=0, Value=[0x00], ErrorType_W=0x8012) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=253, Subindex=1, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2364

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8012

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2365

2366

### Test Specification © IO-Link – 323 – Version V1.1.5

### 2367 9.10.4 ISDU Write to temporarily unavailable Index rejected with ErrorType

### 2368 Table 256 defines the test conditions for this test case.

### 2369 Table 256 – ISDU Write to temporarily unavailable Index rejected with ErrorType

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0226

Name TCM_ALIC_AERR_WRITETEMPUNAV

Purpose (short) ISDU Write to temporarily unavailable Index rejected with ErrorType

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2370

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write to temporarily unavailable Index rejected with ErrorType. Access to a temporarily unavailable Index in the MTU is rejected with an application error. The response reports an ErrorCode "0x8020" (SERV_NOTAVAIL).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=252, Subindex=0, Value=[0x00], ErrorType_W=0x8020) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=252, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2371

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8020

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2372

2373

### Version V1.1.5 – 324 – Test Specification © IO-Link

### 2374 9.10.5 ISDU Write to temporarily unavailable Index due to local control

### 2375 Table 257 defines the test conditions for this test case.

### 2376 Table 257 – ISDU Write to temporarily unavailable Index due to local control

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0227

Name TCM_ALIC_AERR_WRITEINDEXTEMPANAVLC

Purpose (short) ISDU Write to temporarily unavailable Index due to local control

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2377

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to a temporarily unavailable Index due to local control in the MTU is rejected with an application error. The response reports an ErrorCode "0x8021" (SERV_NOTAVAIL_LOCCRTL).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=251, Subindex=0, Value=[0x00], ErrorType_W=0x8021) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=251, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2378

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8021

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2379

2380

### Test Specification © IO-Link – 325 – Version V1.1.5

### 2381 9.10.6 ISDU Write to temporarily unavailable Index due to Device control

### 2382 Table 258 defines the test conditions for this test case.

### 2383 Table 258 – ISDU Write to temporarily unavailable Index due to Device control

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0228

Name TCM_ALIC_AERR_WRITEINDEXTEMPANAVDC

Purpose (short) ISDU Write to temporarily unavailable Index due to Device control

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2384

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to temporarily unavailable Index due to Device control in the MTU is rejected with an application error. The response reports an ErrorCode "0x8022" (SERV_NOTAVAIL_DEVCRTL).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=250, Subindex=0, Value=[0x00], ErrorType_W=0x8022) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=250, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2385

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8022

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2386

2387

### Version V1.1.5 – 326 – Test Specification © IO-Link

### 2388 9.10.7 ISDU Write to read-only Index denied

### 2389 Table 259 defines the test conditions for this test case.

### 2390 Table 259 – ISDU Write to read-only Index denied

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0229

Name TCM_ALIC_AERR_WRITEINDEXRO

Purpose (short) ISDU Write to read-only Index denied

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2391

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to read-only Index in the MTU is denied with an application error. The response reports an ErrorCode "0x8023" (IDX_NOT_ACCESSIBLE).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=249, Subindex=0, Value=[0x00], ErrorType_W=0x8023) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=249, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2392

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8023

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2393

2394

### Test Specification © IO-Link – 327 – Version V1.1.5

### 2395 9.10.8 ISDU Write with invalid Length

### 2396 Table 260 defines the test conditions for this test case.

### 2397 Table 260 – ISDU Write with invalid Length

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0230

Name TCM_ALIC_AERR_WRITEINVALIDLEN

Purpose (short) ISDU Write with invalid Length

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2398

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access with too short data length to an Index in the MTU is rejected with an application error. The response reports an ErrorCode "0x8034" (VAL_LE- NUNDRUN).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=248, Subindex=0, Value=[0x00], ErrorType_W=0x8034) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=248, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2399

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8034

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2400

2401

### Version V1.1.5 – 328 – Test Specification © IO-Link

### 2402 9.10.9 ISDU Write with parameter value out of range

### 2403 Table 261 defines the test conditions for this test case.

### 2404 Table 261 – ISDU Write with parameter value out of range

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0231

Name TCM_ALIC_AERR_WRITEPARAMOUTOFRNG

Purpose (short) ISDU Write with parameter value out of range

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2405

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access with parameter values out of range to an Index in the MTU is rejected with an application error. The response reports an ErrorCode "0x8030" (PAR_VALOUTOFRNG).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16382, Subindex=0, Value=[0x00], ErrorType_W=0x8030) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16382, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2406

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8030

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2407

2408

### Test Specification © IO-Link – 329 – Version V1.1.5

### 2409 9.10.10 ISDU Write with parameter value above limit

### 2410 Table 262 defines the test conditions for this test case.

### 2411 Table 262 – ISDU Write with parameter value above limit

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0232

Name TCM_ALIC_AERR_WRITEPARAMABOVELIMIT

Purpose (short) ISDU Write with parameter value above limit

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2412

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access with parameter values above limit to an Index in the MTU is rejected with an application error. The response reports an ErrorCode "0x8031" (PAR_VALGTLIM).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16381, Subindex=0, Value=[0x00], ErrorType_W=0x8031) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16381, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2413

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8031

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2414

2415

### Version V1.1.5 – 330 – Test Specification © IO-Link

### 2416 9.10.11 ISDU Write with parameter value below limit

### 2417 Table 263 defines the test conditions for this test case.

### 2418 Table 263 – ISDU Write with parameter value below limit

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0233

Name TCM_ALIC_AERR_WRITEPARAMBELOWLIMIT

Purpose (short) ISDU Write with parameter value below limit

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2419

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access with parameter values below limit to an Index in the MTU is rejected with an application error. The response reports an ErrorCode "0x8032" (PAR_VALLTLIM).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16380, Subindex=0, Value=[0x00], ErrorType_W=0x8032) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16380, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2420

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8032

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2421

2422

### Test Specification © IO-Link – 331 – Version V1.1.5

### 2423 9.10.12 ISDU Write with invalid parameter set

### 2424 Table 264 defines the test conditions for this test case.

### 2425 Table 264 – ISDU Write with invalid parameter set

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0234

Name TCM_ALIC_AERR_WRITEPARAMINVALID

Purpose (short) ISDU Write with invalid parameter set

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2426

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access with invalid parameter values to an Index in the MTU is rejected with an application error. For example, lower threshold value is above upper threshold value. The response reports an ErrorCode "0x8040" (PAR_SETINVALID).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16379, Subindex=0, Value=[0x00], ErrorType_W=0x8040) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16379, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2427

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8040

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2428

2429

### Version V1.1.5 – 332 – Test Specification © IO-Link

### 2430 9.10.13 ISDU Write while Device application fault

### 2431 Table 265 defines the test conditions for this test case.

### 2432 Table 265 – ISDU Write while Device application fault

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0235

Name TCM_ALIC_AERR_WRITEDEVICEAPPFAULT

Purpose (short) ISDU Write while Device application fault

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2433

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an Index in the MTU, whose technology specific application is not performing, is rejected with an application error. The response reports an Error- Code "0x8082" (APP_DEVNOTRDY).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16378, Subindex=0, Value=[0x00], ErrorType_W=0x8082) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16378, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2434

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8082

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2435

2436

2437

### Test Specification © IO-Link – 333 – Version V1.1.5

### 2438 9.10.14 ISDU Write to reserved Indices

### 2439 Table 266 defines the test conditions for this test case.

### 2440 Table 266 – ISDU Write to reserved Indices

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0237

Name TCM_ALIC_AERR_WRITERESERVEDINDEX

Purpose (short) ISDU Write to reserved Indices

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Table B.7, Annex C

Configuration / setup Master-Tester-System with MTU 2441

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to a reserved Index in the MTU is rejected with an application error. The response reports an ErrorCode "0x8011" (IDX_NOTAVAIL).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=65025, Subindex=0, Value=[0x00], ErrorType_W=0x8011) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=65025, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2442

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8011

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2443

2444

### Version V1.1.5 – 334 – Test Specification © IO-Link

### 2445 9.10.15 ISDU Write to reserved Indices and no ISDU

### 2446 Table 267 defines the test conditions for this test case.

### 2447 Table 267 – ISDU Write to reserved Indices and no ISDU

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0238

Name TCM_ALIC_AERR_WRITERESERVEDINDEXNOISDU

Purpose (short) ISDU Write to reserved Indices and unavailable ISDU is rejected with ErrorType.

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2448

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to a reserved Index and ISDU not available in MTU is rejected by an application error.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_DI

Procedure a) MTU_DPP1_Set(RevisionID=1.0) b) MTU_DPP1_Set(M-sequenceCapability=0x10);ISDU not supported c) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) d) TM_AWAIT_PORT_STATUS(OPERATE) e) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16, Subindex=0, OD=[0x00])) f) Evaluation 1)

Test parameter –

Post condition – 2449

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check returned ArgBlock

Test passed ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x8011

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2450

2451

### Test Specification © IO-Link – 335 – Version V1.1.5

### 2452 9.11 ISDU (Indexed Service Data Unit) – Derived ErrorTypes

### 2453 9.11.1 ISDU Write response without busy indication

### 2454 Table 268 defines the test conditions for this test case. MTU specialty "NO_DEVICE_BUSY"

### 2455 causes an immediate Device response without busy indication (see A.4.8.3).

### 2456 Table 268 – ISDU Write response without busy indication

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0239

Name TCM_ALIC_DERR_WRITENOBUSY

Purpose (short) ISDU Write response “No service” (without busy) reports Derived ErrorType.

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C

Configuration / setup Master-Tester-System with MTU 2457

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an Index in the MTU. The MTU directly (without any busy response) responds with “No service” (I-Service/Length = 0x00). The response provides the Derived ErrorCode "0x1000" (COM_ERR).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16376, Subindex=0, Value=[0x00], Specialty=NO_SERVICE) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16376, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2458

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x1000

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2459

2460

### Version V1.1.5 – 336 – Test Specification © IO-Link

### 2461 9.11.2 ISDU Write response with timeout after busy indication

### 2462 Table 269 defines the test conditions for this test case. MTU specialty "TIMEOUT" causes an

### 2463 inappropriate delay of Device's response (see A.4.8.3).

### 2464 Table 269 – ISDU Write response with timeout after busy indication

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0240

Name TCM_ALIC_DERR_WRITEAFTERBUSYTIMEOUT

Purpose (short) ISDU Write response with timeout after busy indication reports Derived ErrorType

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], 8.3.2.3, 10.8.7, A.5.2, Annex C, Tables B.7, C.2

Configuration / setup Master-Tester-System with MTU 2465

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an Index in the MTU, which does not generate a response after an adequate time for the ISDU acknowledgement and despite indicating the "Device busy" bit in the ISDU Service/Length octet, is responded with an application error. The response reports a derived ErrorCode "0x1000" (COM_ERR) or "0x1100" (I- SERVICE_TIMEOUT).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16376, Subindex=0, Value=[0x00], Specialty=TIMEOUT) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16376, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2466

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x1000 or 0x1100

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2467

2468

### Test Specification © IO-Link – 337 – Version V1.1.5

### 2469 9.11.3 ISDU Write response with illegal service code

### 2470 Table 270 defines the test conditions for this test case. MTU specialty "INCORRECT_SER-

### 2471 VICE_CODE" causes an incorrect service code in Device's response (see A.4.8.3).

### 2472 Table 270 – ISDU Write response with illegal service code

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0241

Name TCM_ALIC_DERR_ILLSERVICECODE

Purpose (short) ISDU Write response with illegal service code reports Derived ErrorType

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C, Table C.2, Figure A.19

Configuration / setup Master-Tester-System with MTU 2473

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an Index in the MTU generating a response with incorrect service code, is responded with an application error. The response reports a derived ErrorCode "0x5700" (M_ISDU_ILLEGAL).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16374, Subindex=0, Value=[0x00], Specialty=INCORRECT_SERVICE_CODE) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16374, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2474

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x5700

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2475

2476

### Version V1.1.5 – 338 – Test Specification © IO-Link

### 2477 9.11.4 ISDU Write response with wrong checksum (CHKPDU)

### 2478 Table 271 defines the test conditions for this test case. MTU specialty "INCORRECT_CHKPDU"

### 2479 causes a checksum error in Device's response (see A.4.8.3).

### 2480 Table 271 – ISDU Write response with wrong checksum (CHKPDU)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0242

Name TCM_ALIC_DERR_WRONGCHECKSUM

Purpose (short) ISDU Write response with incorrect checksum (CHKPDU) reports Derived ErrorType.

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C, Table C.2, Figure A.19

Configuration / setup Master-Tester-System with MTU 2481

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an Index in the MTU generating a response with wrong CHKPDU, is responded with an application error. The response reports a derived ErrorCode "0x5600" (M_ISDU_CHECKSUM).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16374, Subindex=0, Value=[0x00], Specialty=INCORRECT_CHKPDU) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16374, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter –

Post condition – 2482

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x5600

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2483

2484

### Test Specification © IO-Link – 339 – Version V1.1.5

### 2485 9.11.5 ISDU Write response with reserved data length

### 2486 Table 272 defines the test conditions for this test case. MTU specialty "RESERVED_DATA-

### 2487 _LENGTH" causes reserved combinations of iService and length in Device's response (see

### 2488 A.4.8.3).

### 2489 Table 272 – ISDU Write response with reserved data length

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0244

Name TCM_ALIC_DERR_WRITERESERVEDDL

Purpose (short) ISDU Write response with reserved data length reports Derived ErrorType

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, Annex C, Table C.2, Figure A.19

Configuration / setup Master-Tester-System with MTU 2490

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an Index in the MTU is responded with "reserved data length" and results in an application error. The response reports a Derived ErrorCode "0x1000" (COM_ERR) or 0x5700 (M_ISDU_ILLEGAL).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16372, Subindex=0, Value=[0x00], Specialty=RESERVED_DATA_LENGTH) d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16372, Subindex=0, OD=[0x00])) e) Evaluation 2)

Test parameter -

Post condition - 2491

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "Job Error" received, and JobError.ErrorCode/AdditionalCode = 0x1000 or 0x5700

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2492

2493

### Version V1.1.5 – 340 – Test Specification © IO-Link

### 2494 9.12 ISDU (Indexed Service Data Unit) – Limit checks

### 2495 9.12.1 ISDU Read response without data

### 2496 Table 273 defines the test conditions for this test case.

### 2497 Table 273 – ISDU Read response without data

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0243

Name TCM_ALIC_DERR_READNODATA

Purpose (short) ISDU Read response without data reports no Derived ErrorType

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.7, Annex C, Figure A.19

Configuration / setup Master-Tester-System with MTU 2498

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Read access to an Index in the MTU generating a positive response without data is responded without error.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16372, Subindex=0, Value=[]);prepare MTU d) SMI_DeviceRead(ABPS_DEVICEREAD(Index=16372, Subindex=0)) e) Evaluation 2)

Test parameter –

Post condition – 2499

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "On-request_Data" received, and OD = []

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2500

2501

### Test Specification © IO-Link – 341 – Version V1.1.5

### 2502 9.12.2 ISDU Write with minimum data length (zero octets)

### 2503 Table 274 defines the test conditions for this test case.

### 2504 Table 274 – ISDU Write with minimum data length (zero octets)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0245

Name TCM_ALIC_LIMT_WRITEMINDATALENGTH

Purpose (short) ISDU Write with minimum data length (zero octets)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.4, A.5.7, Annex C, Table A.15, Figure A.18

Configuration / setup Master-Tester-System with MTU 2505

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an Index in the MTU using the minimum data of zero octets. The response shall be positive.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16000, Subindex=0, Value=[]);prepare MTU d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16000, Subindex=0, OD=[])) e) Evaluation 2)

Test parameter –

Post condition – 2506

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "VoidBlock" received

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2507

2508

### Version V1.1.5 – 342 – Test Specification © IO-Link

### 2509 9.12.3 ISDU Write with maximum service length (238 octets)

### 2510 Table 275 defines the test conditions for this test case.

### 2511 Table 275 – ISDU Write with maximum service length (238 octets)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0246

Name TCM_ALIC_LIMT_WRITEMAXDATALENGTH

Purpose (short) ISDU Write with maximum service length (238 octets)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.4, A.5.7, Annex C, Table A.15, Figure A.18

Configuration / setup Master-Tester-System with MTU 2512

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an Index in the MTU using the maximum service length of 238 octets. The response shall be positive.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16001, Subindex=0, Value=<data>);prepare MTU d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16001, Subindex=0, OD=<data>)) e) Evaluation 2) f) MTU_ISDU_Read(Index=16001,Subindex=0);returns <value> (octet string) g) Evaluation 3)

Test parameter data = [0x00, 0x01 to 0xE7] (232 octets user data)

Post condition – 2513

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

3. Check returned <value>

Test passed "OPERATE-reached" = TRUE ArgBlock "VoidBlock" received Returned <value> = data

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2514

2515

### Test Specification © IO-Link – 343 – Version V1.1.5

### 2516 9.12.4 ISDU Read with maximum service length (238 octets)

### 2517 Table 276 defines the test conditions for this test case.

### 2518 Table 276 – ISDU Read with maximum service length (238 octets)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0248

Name TCM_ALIC_LIMT_READMAXDATALENGTH

Purpose (short) ISDU Read with maximum service length (238 octets)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.4, A.5.7, Annex C, Table A.15, Figure A.18

Configuration / setup Master-Tester-System with MTU 2519

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Read access to an Index in the MTU using the maximum service length of 238 octets. The response shall be positive.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16003, Subindex=0, Value=<data>);prepare MTU d) SMI_DeviceRead(ABPS_DEVICEREAD(Index=16003, Subindex=0)) e) Evaluation 2)

Test parameter data = [0x01, 0x02 to 0xE8] (232 octets user data)

Post condition – 2520

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

Test passed "OPERATE-reached" = TRUE ArgBlock "On-request_Data" received, and OD = data

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2521

2522

### Version V1.1.5 – 344 – Test Specification © IO-Link

### 2523 9.12.5 ISDU Write to 8-bit Index and no Subindex

### 2524 Table 277 defines the test conditions for this test case.

### 2525 Table 277 – ISDU Write to 8-bit Index and no Subindex

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0249

Name TCM_ALIC_LIMT_WRITEINDEX8NOSUBINDEX

Purpose (short) ISDU Write to 8-bit Index and no Subindex

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.4, A.5.7, Annex C, Table A.15, Figure A.18

Configuration / setup Master-Tester-System with MTU 2526

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an 8-bit Index in the MTU. The response shall be positive.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=255, Subindex=0, Value=[0xAB]);prepare MTU d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=255, Subindex=0, OD=[0x00])) e) Evaluation 2) f) MTU_ISDU_Read(Index=255, Subindex=0);returns <value> (octet string) g) Evaluation 3)

Test parameter –

Post condition – 2527

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

3. Check returned <value>

Test passed "OPERATE-reached" = TRUE ArgBlock "VoidBlock" received Returned <value> = 0x00

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2528

2529

### Test Specification © IO-Link – 345 – Version V1.1.5

### 2530 9.12.6 ISDU Write to 8-bit Index and 8-bit Subindex

### 2531 Table 278 defines the test conditions for this test case.

### 2532 Table 278 – ISDU Write to 8-bit Index and 8-bit Subindex

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0250

Name TCM_ALIC_LIMT_WRITEINDEX8SUBINDEX8

Purpose (short) ISDU Write to 8-bit Index and 8-bit Subindex

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.4, A.5.7, Annex C, Table A.15, Figure A.18

Configuration / setup Master-Tester-System with MTU 2533

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an 8-bit Index and 8-bit Subindex in the MTU. The response shall be positive.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=255, Subindex=255, Value=[0xAB]);prepare MTU d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=255, Subindex=255, OD=[0x00])) e) Evaluation 2) f) MTU_ISDU_Read(Index=255, Subindex=255);returns <value> (octet string) g) Evaluation 3)

Test parameter –

Post condition – 2534

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

3. Check returned <value>

Test passed "OPERATE-reached" = TRUE ArgBlock "VoidBlock" received Returned <value> = 0x00

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2535

2536

### Version V1.1.5 – 346 – Test Specification © IO-Link

### 2537 9.12.7 ISDU Write to 16-bit Index and 8-bit Subindex

### 2538 Table 279 defines the test conditions for this test case.

### 2539 Table 279 – ISDU Write to 16-bit Index and 8-bit Subindex

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0251

Name TCM_ALIC_LIMT_WRITEINDEX16SUBINDEX8

Purpose (short) ISDU Write to 16-bit Index and 8-bit Subindex

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.4, A.5.7, Annex C, Table A.15, Figure A.18

Configuration / setup Master-Tester-System with MTU 2540

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to a 16-bit Index and 8-bit Subindex in the MTU. The response shall be positive.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=16004, Subindex=1, Value=[0xAB]);prepare MTU d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=16004, Subindex=1, OD=[0x00])) e) Evaluation 2) f) MTU_ISDU_Read(Index=16004, Subindex=1);returns <value> (octet string) g) Evaluation 3)

Test parameter –

Post condition – 2541

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

3. Check returned <value>

Test passed "OPERATE-reached" = TRUE ArgBlock "VoidBlock" received Returned <value> = 0x00

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2542

2543

### Test Specification © IO-Link – 347 – Version V1.1.5

### 2544 9.12.8 ISDU Write response without busy bit

### 2545 Table 280 defines the test conditions for this test case.

### 2546 Table 280 – ISDU Write response without busy bit

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0252

Name TCM_ALIC_LIMT_IMMEDIATERESPNOBUSY

Purpose (short) ISDU Write immediate response (without busy)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.4, A.5.7, Figure A.19, Annex C

Configuration / setup Master-Tester-System with MTU 2547

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an 8-bit Index and 8-bit Subindex in the MTU. MTU generates an immediate response to the request without any busy respones (no I-Service/Length = 0x01 responses, see Table A.14).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=255, Subindex=1, Value=[0xAB], Specialty=NO_DEVICE_BUSY);prepare MTU d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=255, Subindex=1, OD=[0x00])) e) Evaluation 2) f) MTU_ISDU_Read(Index=255, Subindex=1);returns <value> (octet string) g) Evaluation 3)

Test parameter –

Post condition – 2548

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

3. Check returned <value>

Test passed "OPERATE-reached" = TRUE ArgBlock "VoidBlock" received Returned <value> = 0x00

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2549

2550

### Version V1.1.5 – 348 – Test Specification © IO-Link

### 2551 9.12.9 ISDU Write response with busy bit

### 2552 Table 281 defines the test conditions for this test case.

### 2553 Table 281 – ISDU Write response with busy bit

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0253

Name TCM_ALIC_LIMT_IMMEDIATERESPWITHBUSY

Purpose (short) ISDU Write response with at least one busy response

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.4, A.5.7, Figure A.19, Annex C

Configuration / setup Master-Tester-System with MTU 2554

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to an 8-bit Index and 8-bit Subindex in the MTU. MTU generates an ISDU response to the request with at least one busy response (I-Service/Length = 0x01, see Table A.14)

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) MTU_ISDU_Add(Index=255, Subindex=2, Value=[0xAB], Specialty=DEVICE_BUSY);prepare MTU d) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=255, Subindex=2, OD=[0x00])) e) Evaluation 2) f) MTU_ISDU_Read(Index=255, Subindex=2);returns <value> (octet string) g) Evaluation 3)

Test parameter –

Post condition – 2555

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached"

2. Check returned ArgBlock

3. Check returned <value>

Test passed "OPERATE-reached" = TRUE ArgBlock "VoidBlock" received Returned <value> = 0x00

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2556

2557

### Test Specification © IO-Link – 349 – Version V1.1.5

### 2558 9.12.10 ISDU Write with maximum service Length (15 octets)

### 2559 Table 282 defines the test conditions for this test case.

### 2560 Table 282 – ISDU Write with maximum service Length (15 octets)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0254

Name TCM_ALIC_LIMT_WRITEMAXSERVICELEN15

Purpose (short) ISDU service (with maximum service length 15) is carried out.

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.4, A.5.7, Annex C, Table A.15, Figure A.18

Configuration / setup Master-Tester-System with MTU 2561

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to several Index and Subindex combinations in the MTU. MTU with maximum service length 15. The response shall be positive.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) Assign first values to "index", "subindex", "init", "data" d) MTU_ISDU_Add(Index=<index>, Subindex=<subindex>, Value=<init>);prep MTU e) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=<index>, Subindex=<subindex>, OD=<data>)) f) Evaluation 2) g) MTU_ISDU_Read(Index=<index>, Subindex=<subindex>);returns <value> h) Evaluation 3) i) Repeat from c) with next "index", "subindex", "init", "data"

Test parameter index = {100, 101, 16005} subindex = {0, 1, 1} init = { [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00], [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]} data = { [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C], [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B], [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A]}

Post condition – 2562

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached" For all loops:

2. Check returned ArgBlock

3. Check returned <value>

Test passed "OPERATE-reached" = TRUE For all loops: ArgBlock "VoidBlock" received Returned <value> = data

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2563

2564

### Version V1.1.5 – 350 – Test Specification © IO-Link

### 2565 9.12.11 ISDU Write with minimum service Extended Length (17)

### 2566 Table 283 defines the test conditions for this test case.

### 2567 Table 283 – ISDU Write with minimum service Extended Length (17)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0255

Name TCM_ALIC_LIMT_WRITEMINSERVICEEXTLEN17

Purpose (short) ISDU service (with minimum Extended Length 17) is carried out.

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master ISDU test: test to pass

Specification (clause) [5], 8.2.4.1.2; [1], A.5.2, A.5.4, A.5.7, Annex C, Table A.15, Figure A.18

Configuration / setup Master-Tester-System with MTU 2568

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) ISDU Write access to several Index and Subindex combinations in the MTU. MTU with minimum Extended Length 17. The response shall be positive.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "OPERATE-reached" b) Evaluation 1) c) Assign first values to "index", "subindex", "init", "data" d) MTU_ISDU_Add(Index=<index>, Subindex=<subindex>, Value=<init>);prep MTU e) SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=<index>, Subindex=<subindex>, OD=<data>)) f) Evaluation 2) g) MTU_ISDU_Read(Index=<index>, Subindex=<subindex>);returns <value> h) Evaluation 3) i) Repeat from c) with next "index", "subindex", "init", "data"

Test parameter index = {100, 101, 16005} subindex = {0, 1, 1} init = { [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00] [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00] [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]} data = { [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D] [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0c0C] [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B]}

Post condition – 2569

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "OPERATE-reached" For all loops:

2. Check returned ArgBlock

3. Check returned <value>

Test passed "OPERATE-reached" = TRUE For all loops: ArgBlock "VoidBlock" received Returned <value> = data

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2570

2571

### Test Specification © IO-Link – 351 – Version V1.1.5

### 2572 9.13 Events

### 2573 9.13.1 General

### 2574 The Event propagation to the upper-level system, for example a fieldbus, is not subject matter

### 2575 of this document. This behavior shall be defined in the corresponding "upper-level systems

### 2576 integration" specification. Thus, there is no immediate Event acknowledgement of the Master

### 2577 as with the Legacy-Master. Therefore, the timeout for waiting on the acknowledgement shall be

### 2578 adjustable in the MTU.

### 2579 9.13.2 Master receives Events without details (notification)

### 2580 Table 284 defines the test conditions for this test case.

### 2581 Table 284 – Master receives Events without details (notification)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0256

Name TCM_ALIC_EVNT_NODETAILSNOTIFY

Purpose (short) Master receives Event (notification) without details (notification)

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master Event test: test to pass

Specification (clause) [5], 7.2.4.4.2.1; [1], 7.3.8.3, 8.3.3.1, 11.6, Annex A.6, Annex D

Configuration / setup Master-Tester-System 2582

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives Event without details (StatusCode type 1, notification). Master extracts the EventCode (type 1), converts it accordingly into StatusCode (type 2) and transfers it to the upper level system. Master acknowledges the Event. MTU resets the Event flag. Five different combinations are tested in TPLs.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_INACTIVE

Procedure a) MTU_DPP1_Set(RevisionID = 0x10) b) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) c) TM_AWAIT_PORT_STATUS(OPERATE) d) Assign first values to "status_code";see Test parameter e) DLL_ClearAllEvents;clears buffer in test interface f) MTU_Event_Clear;removes Events in memory g) MTU_Event_SetStatusCode(<status_code>);enters StatusCode in memory h) MTU_Event_TriggerAndWaitForAck;returns "EventAck" i) Evaluation 2 j) DLL_GetDeviceEvents(1);minED = 1, returns "EventList" with converted Event k) Evaluation 3) l) Repeat from e) with next "status_code"

Test parameter status_code = {0x01, 0x02, 0x04, 0x08, 0x10} event_code = {0xFF80, 0xFF80, 0x6320, 0xFF80, 0xFF10}

Post condition – 2583

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "EventAck"

2. Check "EventList"

Test passed For all TPLs: "MTU in OPERATE" = TRUE, and EventAck.StatusCodeRead = TRUE, EventAck.Acknowledge = TRUE, and EventList.Entry1 (EventQualifier = 0x54, EventCode = <event_code>);type1 to type2

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2584

### Version V1.1.5 – 352 – Test Specification © IO-Link

### 2585 9.13.3 Master receives event with details (single event)

### 2586 Table 285 defines the test conditions for this test case.

### 2587 Table 285 – Master receives event with details (single event)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0261

Name TCM_ALIC_EVNT_WITHDETAILSSINGLEEVENT

Purpose (short) Master receives Event with details (single Event)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master Event test: test to pass

Specification (clause) [1], 7.3.8.3, 8.3.3.1, 11.5, Annex A.6, Annex D

Configuration / setup Master-Tester-System 2588

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives Event with details (StatusCode type 2). Master transfers the EventCode and EventQuaifier to the upper-level system. Master acknowledges the Event. MTU resets the Event flag.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) MTU_Event_SetStatusCode(0x81);enters StatusCode in memory d) MTU_Event_SetSlot(1, 0xF4, 0x1000);enters in slot 1 Event Q and Code e) MTU_Event_TriggerAndWaitForAck;returns "EventAck" f) Evaluation 2 g) DLL_GetDeviceEvents(1);minED = 1, returns "EventList" h) Evaluation 3)

Test parameter –

Post condition – 2589

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "EventAck"

3. Check "EventList"

Test passed "MTU in OPERATE" = TRUE, and EventAck.StatusCodeRead = TRUE, EventAck.AllSlotsRead = TRUE, EventAck.Acknowledge = TRUE, and EventList.Entry1 (EventQualifier = 0xF4, EventCode = 0x1000)

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2590

2591

### Test Specification © IO-Link – 353 – Version V1.1.5

### 2592 9.13.4 Master receives event with details (double event)

### 2593 Table 286 defines the test conditions for this test case.

### 2594 Table 286 – Master receives event with details (double event)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0262

Name TCM_ALIC_EVNT_WITHDETAILSDOUBLEEVENT

Purpose (short) Master receives Event with details (double Event)

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master Event test: test to pass

Specification (clause) [1], 7.3.8.3, 8.3.3.1, 11.5, Annex A.6, Annex D

Configuration / setup Master-Tester-System 2595

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives Event with details (StatusCode type 2). Master transfers it to the upper level system. Master acknowledges the Event. The MTU resets the Event flag. This procedure can be carried out in loops for different Events in the Event buffer (TPL).

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) Assign first values to "status_code", "slot_a", "slot_b",”qual_a”, “qual_b” d) DLL_ClearAllEvents;clears buffer in test interface e) MTU_Event_Clear;removes Events in memory f) MTU_Event_SetStatusCode(<status_code>);enters StatusCode in memory g) MTU_Event_SetSlot(<slot_a>, <qual_a>, 0x1000);enters Event 1 in memory h) MTU_Event_SetSlot(<slot_b>, <qual_b>, 0x4210);enters Event 2 in memory i) MTU_Event_TriggerAndWaitForAck;returns "EventAck" j) Evaluation 2 k) DLL_GetDeviceEvents(2);minED = 2, returns "EventList" l) Evaluation 3) m) wait for 1 second; to ensure that the event rules are respected n) Repeat from d) with next "status_code", “slot_a”, “slot_b”, “qual_a”,”qual_b”

Test parameter status_code = {0xA1, 0x92, 0x8C, 0x8C, 0x92, 0xA1} slot_a = {1, 2, 3, 4, 5, 6} slot_b = {6, 5, 4, 3, 2, 1} qual_a = {0xF4, 0xB4, 0xE4, 0xA4, 0xF4, 0xB4} qual_b = {0xE4, 0xA4, 0xF4, 0xB4, 0xE4, 0xA4}

Post condition – 2596

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "EventAck"

3. Check "EventList"

Test passed For all TPLs: "MTU in OPERATE" = TRUE, and EventAck: StatusCodeRead = TRUE, AllSlotsRead = TRUE, Acknowledge = TRUE, and EventList: 2 elements EventList: Entry1 (EventQualifier = “qual_a”, EventCode = 0x1000), Entry2 (EventQualifier = “qual_b”, EventCode = 0x4210)

Test not passed (examples) Any evaluation failed

### Version V1.1.5 – 354 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

Report All evaluations <ok | nok>

2597

2598

### Test Specification © IO-Link – 355 – Version V1.1.5

### 2599 9.13.5 Events of type notification

### 2600 Table 287 defines the test conditions for this test case.

### 2601 Table 287 – Events of type notification

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0308

Name TCD_DLIC_EVNTOPER_SINGLENOTIFICATION

Purpose (short) Master receives Event with details (single Event, notification)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master Event test: test to pass

Specification (clause) [1], 7.3.8.3, 8.3.3.1, 11.5, Annex A.6, Annex D

Configuration / setup Master-Tester-System 2602

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives Event with details (StatusCode type 2, notification). Master transfers the EventCode and EventQualifier to the upper-level system (optional). Master acknowledges the Event. MTU resets the Event flag.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) MTU_Event_SetStatusCode(0x81);enters StatusCode in memory d) MTU_Event_SetSlot(1, 0x54, 0x1800);enters Event in memory e) MTU_Event_TriggerAndWaitForAck;returns "EventAck" f) Evaluation 2 g) DLL_GetDeviceEvents(1);minED = 1, returns "EventList" h) Evaluation 3) i) SMI_PortStatus;returns "ArgBlock PortStatusList" j) Evaluation 4)

Test parameter –

Post condition / next test – 2603

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "EventAck"

3. Check "EventList"

4. Check "ArgBlock PortStatusList"

Test passed "MTU in OPERATE" = TRUE, and EventAck.StatusCodeRead = TRUE, EventAck.AllSlotsRead = TRUE, EventAck.Acknowledge = TRUE, and EventList.Entry1 (EventQualifier = 0x54, EventCode = 0x1800), and;"No Device" PortStatusList.NumberOfDiags = 0

Test not passed Any evaluation failed

Report All evaluations <ok | nok>

2604

### Version V1.1.5 – 356 – Test Specification © IO-Link

### 2605 9.13.6 Master receives Event with details (six Events)

### 2606 Table 288 defines the test conditions for this test case.

### 2607 Table 288 – Master receives Event with details (six Events)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0263

Name TCM_ALIC_EVNT_WITHDETAILSSIXEVENTS

Purpose (short) Master receives event with details (six Events)

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master Event test: test to pass

Specification (clause) [5], 7.2.4.4.2.1; [1], 7.3.8.3, 8.3.3.1, 11.5, Annex A.6

Configuration / setup Master-Tester-System 2608

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives six Events with details (StatusCode type 2). Master transfers it to the upper-level system. Master acknowledges the Event. MTU resets the Event flag.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) MTU_Event_SetStatusCode(0xBF);enters StatusCode in memory d) MTU_Event_SetSlot(1, 0xF4, 0x1800);enter Event appears, Error e) MTU_Event_SetSlot(2, 0xE4, 0x1801);enter Event appears, Warning f) MTU_Event_SetSlot(3, 0x54, 0x1802);enter Event single shot, Notification g) MTU_Event_SetSlot(4, 0xB4, 0x1803);enter Event disappears, Error h) MTU_Event_SetSlot(5, 0xA4, 0x1804);enter Event disappers, Warning i) MTU_Event_SetSlot(6, 0x54, 0x1805);enter Event single shot, Notification j) MTU_Event_TriggerAndWaitForAck;returns "EventAck" k) Evaluation 2 l) DLL_GetDeviceEvents(6);minED = 6, returns "EventList" m) Evaluation 3) n) SMI_PortStatus;returns "ArgBlock PortStatusList" o) Evaluation 4)

Test parameter –

Post condition – 2609

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "EventAck"

3. Check "EventList"

4. Check "ArgBlock PortStatusList"

Test passed "MTU in OPERATE" = TRUE, and EventAck.StatusCodeRead = TRUE, EventAck.AllSlotsRead = TRUE, EventAck.Acknowledge = TRUE, and EventList.Entry1 (EventQualifier = 0xF4, EventCode = 0x1800), EventList.Entry2 (EventQualifier = 0xE4, EventCode = 0x1801), EventList.Entry3 (EventQualifier = 0x54, EventCode = 0x1802), EventList.Entry4 (EventQualifier = 0xB4, EventCode = 0x1803), EventList.Entry5 (EventQualifier = 0xA4, EventCode = 0x1804), EventList.Entry6 (EventQualifier = 0x54, EventCode = 0x1805), and PortStatusList.NumberOfDiags = 2, PortStatusList.DiagEntry0: Qualifier = 0xF4, Code = 0x1800 PortStatusList.DiagEntry1: Qualifier = 0xE4, Code = 0x1801

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

Test Specification © IO-Link – 357 – Version V1.1.5

2610 2611

### Version V1.1.5 – 358 – Test Specification © IO-Link

### 2612 9.13.7 Master receives Event while in ISDU Write transfer (stopover)

### 2613 Table 289 defines the test conditions for this test case.

### 2614 Table 289 – Master receives Event while in ISDU Write transfer (stopover)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0264

Name TCM_ALIC_EVNT_WRITEISDUWITH EVENT

Purpose (short) Master receives one Event while in ISDU transfer (stopover; no details)

Equipment under test (EUT) Master + Port

Test case version 1.3

Category / type Master Event test: test to pass

Specification (clause) [1], 7.3.8.3, 8.3.3.1, 11.5, Annex A.6, Annex I

Configuration / setup Master-Tester-System 2615

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives one Event while in ISDU Write transfer (stopover; no details). The Event flag shall be set during transfer of an ISDU. The Master interrupts the ISDU and retrieves the Event content. After Event handling, the ISDU transfer shall be continued.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) MTU_ISDU_Add(Index=0x18,Subindex=0, Value=[0xAB,0xCD,0x12,0x34],Specialty=EVENT);test data d) MTU_Event_SetStatusCode(0x01);enters StatusCode in memory e)SMI_DeviceWrite(ABPS_DEVICEWRITE(Index=0x18,Subindex=0, Value=[0x00,0x00,0x00,0x00]));returns "ArgBlock" f) Evaluation 2) g) DLL_GetDeviceEvents(1);minED = 1, returns "EventList" h) Evaluation 3) i) MTU_Event_ISDUinterrupted;returns "Interrupted" j) Evaluation 4) k) MTU_ISDU_Read(Index=0x18, Subindex=0);returns "Value" l) Evaluation 5)

Test parameter –

Post condition – 2616

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "Argblock"

3. Check "EventList"

4. Check "Interrupted"

5. Check "Value"

Test passed "MTU in OPERATE" = TRUE, and "Argblock" = VoidBlock, and EventList.Entry1 (EventQualifier = 0x54, EventCode = 0xFF80), and "Interrupted" = TRUE, and "Value" = [0x00, 0x00, 0x00, 0x00]

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2617

2618

### Test Specification © IO-Link – 359 – Version V1.1.5

### 2619 9.13.8 Master receives Event while in ISDU Read transfer (stopover)

### 2620 Table 290 defines the test conditions for this test case.

### 2621 Table 290 – Master receives Event while in ISDU Read transfer (stopover)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0265

Name TCM_ALIC_EVNT_READISDUWITH EVENT

Purpose (short) Master receives Event while in ISDU Read transfer (stopover; no details)

Equipment under test (EUT) Master + Port

Test case version 1.3

Category / type Master Event test: test to pass

Specification (clause) [1], 7.3.8.3, 8.3.3.1, 11.5, Annex A.6

Configuration / setup Master-Tester-System 2622

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives one Event while in ISDU Read transfer (stopover; no details). The Event flag shall be set during transfer of an ISDU. The Master interrupts the ISDU and retrieves the Event content. After Event handling, the ISDU transfer shall be continued.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) MTU_ISDU_Add(Index=0x18, Subindex=0, Value=[0xAB,0xCD,0x12,0x34], Specialty=EVENT);test data d) MTU_Event_SetStatusCode(0x01);enters StatusCode in memory e) SMI_DeviceRead(ABPS_DEVICEREAD (Index=0x18, Subindex=0));returns "ArgBlock" f) Evaluation 2) g) DLL_GetDeviceEvents(1);minED = 1, returns "EventList" h) Evaluation 3) i) MTU_Event_ISDUinterrupted;returns "Interrupted" j) Evaluation 4)

Test parameter –

Post condition – 2623

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "Argblock"

3. Check "EventList"

4. Check "Interrupted"

Test passed "MTU in OPERATE" = TRUE, and ArgBlock.OD = [0xAB, 0xCD, 0x12, 0x34], and EventList.Entry1 (EventQualifier = 0x54, EventCode = 0xFF80), and "Interrupted" = TRUE

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2624

2625

### Version V1.1.5 – 360 – Test Specification © IO-Link

### 2626 9.13.9 Master receives Event details while in ISDU Write transfer (stopover)

### 2627 Table 291 defines the test conditions for this test case.

### 2628 Table 291 – Master receives Event details while in ISDU Write transfer (stopover)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0266

Name TCM_ALIC_EVNT_WRITEISDUWITH EVENTDETAILS

Purpose (short) Master receives Event while in ISDU Write transfer (stopover; with details)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master Event test: test to pass

Specification (clause) [1], 7.3.8.3, 8.3.3.1, 11.5, Annex A.6

Configuration / setup Master-Tester-System 2629

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives Event while in ISDU Write transfer (stopover; with details). The Event flag shall be set during transfer of an ISDU. The Master interrupts the ISDU and retrieves the Event content. After Event handling, the ISDU transfer shall be continued.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) MTU_ISDU_Add(Index=0x18,Subindex=0, Value=[0xAB,0xCD,0x12,0x34],Specialty=EVENT);test data d) MTU_Event_SetStatusCode(0x81);enters StatusCode in memory e) MTU_Event_SetSlot(1, 0xF4, 0x1000);enter Event appears, unknown Error f) SMI_DeviceWrite(ABPS_DEVICE_WITE(Index=0x18,Subindex=0, Value=[0x00,0x00,0x00,0x00]));returns "ArgBlock" g) Evaluation 2) h) DLL_GetDeviceEvents(1);minED = 1, returns "EventList" i) Evaluation 3) j) MTU_Event_ISDUinterrupted;returns "Interrupted" k) Evaluation 4)

Test parameter –

Post condition - 2630

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "Argblock"

3. Check "EventList"

4. Check "Interrupted"

Test passed "MTU in OPERATE" = TRUE, and "Argblock" = VoidBlock, and EventList.Entry1 (EventQualifier = 0xF4, EventCode = 0x1000), and "Interrupted" = TRUE

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2631

2632

### Test Specification © IO-Link – 361 – Version V1.1.5

### 2633 9.13.10 Master receives Event details while in ISDU Read transfer (stopover)

### 2634 Table 292 defines the test conditions for this test case.

### 2635 Table 292 – Master receives Event details while in ISDU Read transfer (stopover)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0267

Name TCM_ALIC_EVNT_READISDUWITH EVENTDETAILS

Purpose (short) Master receives event while in ISDU Read transfer (stopover; with details)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master Event test: test to pass

Specification (clause) [1], 7.3.8.3, 8.3.3.1, 11.5, Annex A.6

Configuration / setup Master-Tester-System 2636

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives one Event while in ISDU Read transfer (stopover; with details). The Event flag shall be set during transfer of an ISDU. The Master interrupts the ISDU and retrieves the Event content. After Event handling, the ISDU transfer shall be continued.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) MTU_ISDU_Add(Index=0x18,Subindex=0, Value=[0xAB,0xCD,0x12,0x34], Specialty=EVENT);test data d) MTU_Event_SetStatusCode(0x81);enters StatusCode in memory e) MTU_Event_SetSlot(1, 0xF4, 0x1000);enter Event appears, unknown Error f) SMI_DeviceRead(ABPS_DEVICEREAD (Index=0x18,Subindex=0));returns "ArgBlock" g) Evaluation 2) h) DLL_GetDeviceEvents(1);minED = 1, returns "EventList" i) Evaluation 3) j) MTU_Event_ISDUnterrupted;returns "Interrupted" k) Evaluation 4)

Test parameter –

Post condition – 2637

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "Argblock"

3. Check "EventList"

4. Check "Interrupted"

Test passed "MTU in OPERATE" = TRUE, and ArgBlock.OD = [0xAB, 0xCD, 0x12, 0x34], and EventList.Entry1 (EventQualifier = 0xF4, EventCode = 0x1000), and "Interrupted" = TRUE

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2638

2639

### Version V1.1.5 – 362 – Test Specification © IO-Link

### 2640 9.13.11 Behavior and Port Event at Device lost (COMLOST)

### 2641 Table 293 defines the test conditions for this test case.

### 2642 Table 293 – Behavior and Port Event at Device lost (COMLOST)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0350

Name TCM_ALIC_EVNT_NODEV

Purpose (short) Master sends Port Event "No Device" at COMLOST

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master Event test: test to pass

Specification (clause) [1], Annex D.3

Configuration / setup Master-Tester-System 2643

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master is expected to send Port Event "No Device" (0x1800) and to clear Device Events in DiagEntries of ArgBlock "PortStatusList" whenever Device gets COMLOST

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_AUTOSTART

Procedure a) MTU_State_CheckOperate;returns "MTU in OPERATE" b) Evaluation 1) c) DLL_ClearAllEvents;clear Event entries d) MTU_Event_SetStatusCode(0x81);enters StatusCode in memory e) MTU_Event_SetSlot(1, 0xF4, 0x1800);enters in slot 1 Event Q and Code f) MTU_Event_TriggerAndWaitForAck;returns "EventAck" (0x1800) g) Evaluation 2) h) DLL_GetDeviceEvents(1);minED=1, returns "DeviceEventList1" i) Evaluation 3) j) SMI_PortStatus;returns "ArgBlock PortStatusList1" k) Evaluation 4) l) MTU_State_Deactivate;disable MTU response m) TM_AWAIT_PORT_STATUS(NO_DEVICE) n) DLL_GetPortEvents(1);minEP=1, returns "PortEventList1" o) Evaluation 5) p) SMI_PortStatus;returns "ArgBlock PortStatusList2" q) Evaluation 6) r) MTU_State_Activate;enable MTU response s) TM_AWAIT_PORT_STATUS(OPERATE) t) DLL_GetPortEvents(1);minEP=1, returns "PortEventList2" u) Evaluation 7) v) SMI_PortStatus;returns "ArgBlock PortStatusList3" w) Evaluation 8)

Test parameter –

Post condition – 2644

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "MTU in OPERATE"

2. Check "EventAck"

3. Check "DeviceEventList1"

4. Check "ArgBlock PortStatusList1"

5. Check "PortEventList1"

6. Check "ArgBlock PortStatusList2"

7. Check "PortEventList2"

8. Check "ArgBlock PortStatusList3"

Test passed "MTU in OPERATE" = TRUE, and EventAck: StatusCodeRead = TRUE, AllSlotsRead = TRUE, Acknowledge = TRUE, and DeviceEventList1.Entry1 (EventQualifier = 0xF4, EventCode = 0x1800), and PortStatusList1: NumberOfDiags = 1, DiagEntry0 (EventQualifier = 0xF4, EventCode = 0x1800), and

### Test Specification © IO-Link – 363 – Version V1.1.5

PortEventList1: Entry1 (EventQualifier = 0xFC, EventCode = 0x1800), and PortStatusList2: NumberOfDiags = 1, DiagEntry0 (EventQualifier = 0xFC, EventCode = 0x1800), and PortEventList2: Entry1 (EventQualifier = 0x5C, EventCode = 0xFF26), or Entry1 (EventQualifier = 0xBC, EventCode = 0x1800), and PortStatusList3: NumberOfDiags = 0

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2645

2646

### Version V1.1.5 – 364 – Test Specification © IO-Link

### 2647 9.13.12 Event in PREOPERATE

### 2648 Table 294 defines the test conditions for this test case.

### 2649 Table 294 – Event in PREOPERATE

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0351

Name TCM_ALIC_EVNT_PREOPERATE

Purpose (short) MTU sends Event in PREOPERATE

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master Event test: test to pass

Specification (clause) [1], Annex D.3

Configuration / setup Master-Tester-System 2650

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives Event with details (StatusCode type 2) in PREOPERATE. Master transfers the EventCode and EventQuaifier to the upper-level system. Master acknowledges the Event. MTU resets the Event flag.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_DI

Procedure a) MTU_Event_SetStatusCode(0x81);enters StatusCode in memory b) MTU_Event_SetSlot(1, 0xF4, 0x1000);enters in slot 1 Event Q and Code c) MTU_Event_SetFlag;set Event flag in M-sequence CKS d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) e) TM_AWAIT_PORT_STATUS(OPERATE) f) DLL_GetDeviceEvents(1);minED=1, returns "EventList" g) Evaluation 1) h) SMI_PortStatus;returns "ArgBlock PortStatusList" i) Evaluation 2)

Test parameter –

Post condition – 2651

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "EventList"

2. Check "ArgBlock PortStatusList"

Test passed EventList.Entry1 (EventQualifier = 0xF4, EventCode = 0x1000), and PortStatusList.NumberOfDiags = 1, PortStatusList.DiagEntry0 (EventQualifier = 0xF4, EventCode = 0x1000)

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2652

2653

### Test Specification © IO-Link – 365 – Version V1.1.5

### 2654 9.13.13 Event in PORT_DIAG

### 2655 Table 295 defines the test conditions for this test case.

### 2656 Table 295 – Event in PORT_DIAG

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0352

Name TCM_ALIC_EVNT_PORT_DIAG

Purpose (short) MTU sends Event in PORT_DIAG

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master Event test: test to pass

Specification (clause) [1], Annex D.3

Configuration / setup Master-Tester-System 2657

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives Event with details (StatusCode type 2) in PORT_DIAG. Master transfers the EventCode and EventQuaifier to the upper level system. Master acknowledges the Event. MTU resets the Event flag.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_DI

Procedure a) MTU_Event_SetStatusCode(0x81);enters StatusCode in memory b) MTU_Event_SetSlot(1, 0xF4, 0x1800);enters in slot 1 Event Q and Code c) MTU_Event_SetFlag;set Event flag in M-sequence CKS d) SMI_PortConfiguration(ABPS_TYPE_COMP <VendorID = 1>);invalid VendorID e) TM_AWAIT_PORT_STATUS(PORT_DIAG) f) DLL_GetDeviceEvents(1);minED=1, returns "DeviceEventList" g) Evaluation 1) h) DLL_GetPortEvents(1);minEP=1, returns "PortEventList" i) Evaluation 2) j) SMI_PortStatus;returns "ArgBlock PortStatusList" k) Evaluation 3)

Test parameter –

Post condition – 2658

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "DeviceEventList"

2. Check "PortEventList"

3. Check "ArgBlock PortStatusList"

Test passed DeviceEventList.Entry1 (EventQualifier = 0xF4, EventCode = 0x1800), and PortEventList.Entry1 (EventQualifier = 0xFC, EventCode = 0x1802), and PortStatusList.NumberOfDiags = 2, PortStatusList.DiagEntry0 (EventQualifier = 0xF4, EventCode = 0x1800), PortStatusList.DiagEntry1 (EventQualifier = 0xFC, EventCode = 0x1802)

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2659

2660

### Version V1.1.5 – 366 – Test Specification © IO-Link

### 2661 9.13.14 Invalid cycle time requested

### 2662 Table 296 defines the test conditions for this test case.

### 2663 Table 296 – Invalid cycle time requested

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0353

Name TCM_ALIC_EVNT_INVALID_CYCLE_TIME

Purpose (short) Master sends Port Event 0x6000 due to a requested invalid cycle time

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master Event test: test to pass

Specification (clause) [1], Annex D.3

Configuration / setup Master-Tester-System 2664

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) MTU is configured to a minimal cycle time of 10 ms. Master port is configured to a cycle time of 4 ms. Since the requested cycle time cannot be achieved, the Master enters PORT_DIAG and sends the Port Event 0x6000 "Invalid cycle time".

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_DI

Procedure a) MTU_DPP1_Set(MinCycleTime = 10 ms) b) SMI_PortConfiguration(ABPS_TYPE_COMP <PortCycleTime = 4 ms>) c) TM_AWAIT_PORT_STATUS(PORT_DIAG) d) DLL_GetPortEvents(1);minEP=1, returns "PortEventList" e) Evaluation 1) f) SMI_PortStatus;returns "ArgBlock PortStatusList" g) Evaluation 2)

Test parameter –

Post condition – 2665

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "PortEventList"

2. Check "ArgBlock PortStatusList"

Test passed EventList.Entry1 (EventQualifier = 0xFC, EventCode = 0x6000), and PortStatusList.NumberOfDiags = 1 PortStatusList.DiagEntry0 (EventQualifier = 0xFC, EventCode = 0x6000)

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2666

2667

### Test Specification © IO-Link – 367 – Version V1.1.5

### 2668 9.14 Data Storage (DS)

### 2669 9.14.1 General

### 2670 Some test cases need cleared Data Storage as a precondition to perform the test. One possi-

### 2671 bility is the re-configuration of the Master Port.

### 2672 9.14.2 Delete stored DS object after reconfiguration

### 2673 Table 297 defines the test conditions for this test case.

### 2674 Table 297 – Delete stored DS object after reconfiguration

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0354

Name TCM_ALIC_STOR_DELETEDSAFTERRECONF

Purpose (short) Delete DS data object after Port reconfiguration

Equipment under test (EUT) Master and Master Port

Test case version 1.1

Category / type Master DS test: test to pass

Specification (clause) [1], 11.3, Figure 101, Table E.3, Annex G, Table G.2

Configuration / setup Master-Tester-System with MTU 2675

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Any relevant change of Port configuration ("PortConfigList") via SMI service causes the stored DataStorageObject to be cleared. Port reconfigurations to different PortModes are tested during test procedure loops (TPL): TPL1: Change to PortMode = DI_C/Q;see [1], Table E.3 TPL2: Change to PortMode = DO_C/Q TPL3: Change to PortMode = IOL_AUTOSTART TPL4: Change to PortMode = IOL_MANUAL and DeviceID = 0x002BD4, Validation&Backup = "4";Restore

TPL5: Change to PortMode = DEACTIVATED TPL6: Change to PortMode = IOL_MANUAL and Validation&Backup = "4";Restore

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "ConfigList" b) TM_MASTER_UPLOAD (PARSET1);prepare EUT and MTU for identical DS data c) SMI_DSToParServ;returns "DS_Data" (uploaded) d) Evaluation 1) e) SMI_PortConfiguration(<ConfigList>);change Port configurationTC_361 f) SMI_DSToParServ;returns "DS_Data" (cleared) g) Evaluation 2) h) Repeat from b) with next "ConfigList"

Test parameter ConfigList = { APBS_PORTTODI, APBS_PORTTODO, APBS_NOTYPE_CHECK, ABPS_TYPE_COMP(<DeviceID = 0x002BD4>,<Validation&Backup = "4”>), ABPS_PORTINACTIVE, ABPS_TYPE_COMP(<Validation&Backup = "4">) }

Post condition / next test – 2676

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check ArgBlock "DS_Data"

2. Check ArgBlock "DS_Data"

### Version V1.1.5 – 368 – Test Specification © IO-Link

TEST CASE RESULTS CHECK / REACTION

Test passed For all TPL: DataStorageObject = PARSET1 in 1), and DataStorageObject = no data/invalid (Header = "0") and length = 12 octets in 2)

Test not passed (examples) Any evaluation failed or communication error

Report All evaluations <ok | nok>

2677

2678

### Test Specification © IO-Link – 369 – Version V1.1.5

### 2679 9.14.3 Data Storage size limits (quantities)

### 2680 Table 298 defines the test conditions for this test case.

### 2681 Table 298 – Data Storage size limits (quantities)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0355

Name TCM_ALIC_STOR_MAXSIZELIMITS

Purpose (short) Check the maximum size limits of DS upload/download

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4

Configuration / setup Master-Tester-System with MTU 2682

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Checks the maximum conditions of DS behavior with respect to Index_List and DS memory size. Using the two data sets in "Test parameter", DS works correctly during the 2 test procedure loops (TPL): TPL1: Maximum permitted size of the DS data object and maximum length of Index_List (MAXINDEXLIST) TPL2: Maximum length of data objects (MAXDATA)

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "config" b) MTU_DS_SetMaxDataStorage(<config>);provide MTU with max DS data c) SMI_PortConfiguration(ABPS_TYPE_COMP <Validation&Backup = "3">) d) TM_AWAIT_PORT_STATUS(OPERATE) e) MTU_DS_CheckUpload;returns "upload sequence performed" f) Evaluation 1 g) SMI_DSToParServ;returns "DS_Data" (uploaded) h) Evaluation 2 i) Repeat from b) with next "config"

Test parameter config = {MAXINDEXLIST, MAXDATA} See A.4.8

Post condition / next test – 2683

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "upload sequence performed"

2. Check ArgBlock "DS_Data"

Test passed TPL1: "upload sequence performed" = TRUE, and DataStorageObject = MAXINDEXLIST TPL2: "upload sequence performed" = TRUE, and DataStorageObject = MAXDATA

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2684

2685

### Version V1.1.5 – 370 – Test Specification © IO-Link

### 2686 9.14.4 Write consistent DS data object from parameter server

### 2687 Table 299 defines the test conditions for this test case.

### 2688 Table 299 – Write consistent DS data object from parameter server

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0356

Name TCM_ALIC_STOR_DSFROMPARAMSERV

Purpose (short) Write consistent DS data object from upper level system

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Annex G

Configuration / setup Master-Tester-System with MTU 2689

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Upper level system writes DS data object with header information via service SMI_ParServToDS. There is no difference between current stored DS data object and written DS data object (match of checksum). Master does not perform any upload or download activities at identical DS data.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) TM_MASTER_UPLOAD(PARSET1);prepare EUT and MTU for identical DS data b) SMI_ParServToDS (PARSET1);write identical DS data (checksum match) c) MTU_DS_CheckDownload;returns "download sequence performed" d) Evaluation 1)

Test parameter –

Post condition / next test – 2690

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "download sequence performed"

Test passed "download sequence performed" = FALSE

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2691

2692

### Test Specification © IO-Link – 371 – Version V1.1.5

### 2693 9.14.5 Write inconsistent DS data object from parameter server

### 2694 Table 300 defines the test conditions for this test case.

### 2695 Table 300 – Write inconsistent DS data object from parameter server

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0357

Name TCM_ALIC_STOR_INCONSDSFROMPARSERV

Purpose (short) Write inconsistent DS data object from upper-level system (checksum mismatch)

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4

Configuration / setup Master-Tester-System with MTU 2696

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Upper-level system writes DS data object with header information via service SMI_ParServToDS. There is a difference between current stored DS data object and written DS data object (mismatch of checksum). This causes the Master to restart the Port and to download DS data.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) TM_MASTER_UPLOAD(PARSET1);prepare EUT and MTU for identical DS data b) SMI_ParServToDS(PARSET2);write different DS data (checksum mismatch) c) TM_AWAIT_PORT_STATUS (OPERATE);wait until OPERATE d) MTU_State_CheckPreoperate;returns "PREOPERATE" e) Evaluation 1) f) MTU_DS_CheckDownload;returns "download sequence performed" g) Evaluation 2) h) MTU_DS_CheckParameterSet(PARSET2);returns "PARSET2 active" i) Evaluation 3)

Test parameter –

Post condition / next test – 2697

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "PREOPERATE"

2. Check "download sequence performed"

3. Check "PARSET2 active"

Test passed "PREOPERATE" = PASSED, and "download sequence performed" = TRUE, and "PARSET2 active" = TRUE

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2698

2699

### Version V1.1.5 – 372 – Test Specification © IO-Link

### 2700 9.14.6 Write DS data object to improper Port configuration

### 2701 Table 301 defines the test conditions for this test case.

### 2702 Table 301 – Write DS data object to improper Port configuration

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0358

Name TCM_ALIC_STOR_DSTOIMPROPPORTCONF

Purpose (short) Write DS data object from upper-level system to improper Port configuration

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Table E.3, Annex G

Configuration / setup Master-Tester-System with MTU 2703

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Write DS data from upper-level system via SMI_ParServToDS. In case of improper Port configuration (PortMode = DI_C/Q, DO_C/Q, IOL_AUTOSTART, or VendorID and/or DeviceID ≠ values in DS data header), the service returns a negative response, and no download activity occurs.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) SMI_PortConfiguration(ABPS_NOTYPE_CHECK);improper configuration for DS b) TM_AWAIT_PORT_STATUS(OPERATE);wait until OPERATE c) SMI_ParServToDS(PARSET2);PortMode prevents from performance d) Evaluation 1) e) MTU_DS_CheckDownload;returns "download sequence performed" f) Evaluation 2)

Test parameter –

Post condition / next test – 2704

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check returned ArgBlock

2. Check "download sequence performed"

Test passed ArgBlock "Job Error" received, JobError.ErrorCode/AdditionalCode = 0x4039 (INCONSISTENT_DS_DATA), and "download sequence performed" = FALSE

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2705

2706

### Test Specification © IO-Link – 373 – Version V1.1.5

### 2707 9.14.7 Upload request Event in OPERATE (Backup + Restore)

### 2708 Table 302 defines the test conditions for this test case.

### 2709 Table 302 – Upload request Event in OPERATE (Backup + Restore)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0359

Name TCM_ALIC_STOR_UPLOADREQEVENTOPERBACKREST

Purpose (short) Detection of upload request Event in OPERATE and Backup + Restore

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Table E.3

Configuration / setup Master-Tester-System with MTU 2710

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) MTU generates a hidden "DS_UPLOAD_REQ" Event while in OPERATE. Master Port configuration is proper for DS (PortMode and Validation&Backup = "3"). Master uploads parameter values from Device.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) TM_MASTER_UPLOAD(PARSET1);prepare EUT and MTU for identical DS data b) MTU_Set_ParameterSet(PARSET2);MTU activates PARSET2 c) MTU_DS_SetUpload(Event);MTU generates DS_UPLOAD_REQ Event d) TM_AWAIT(2000);wait 2 sec e) MTU_DS_CheckUpload;returns "upload sequence performed" f) Evaluation 1) g) SMI_DSToParServ;returns "DS_Data" (uploaded) h) Evaluation 2)

Test parameter –

Post condition / next test – 2711

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "upload sequence performed"

2. Check ArgBlock "DS_Data"

Test passed "upload sequence performed" = TRUE, and DataStorageObject = PARSET2

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2712

2713

### Version V1.1.5 – 374 – Test Specification © IO-Link

### 2714 9.14.8 Upload request Event in OPERATE (Restore)

### 2715 Table 303 defines the test conditions for this test case.

### 2716 Table 303 – Upload request Event in OPERATE (Restore)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0360

Name TCM_ALIC_STOR_UPLOADREQEVENTOPERREST

Purpose (short) Detection of upload request Event in OPERATE and Restore

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Table E.3

Configuration / setup Master-Tester-System with MTU 2717

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) MTU generates a hidden "DS_UPLOAD_REQ" Event while in OPERATE. Master Port configuration is proper for DS (PortMode and Validation&Backup = "4"). Master downloads parameter values to Device.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) TM_MASTER_UPLOAD(PARSET1);prepare EUT and MTU for identical DS data b) SMI_PortConfiguration (ABPS_TYPE_COMP,< Validation&Backup = "4">) c) TM_AWAIT_PORT_STATUS (OPERATE) d) MTU_DS_Set_Parameter(PARSET2);MTU activates PARSET2 c) MTU_DS_SetUpload(Event);MTU generates DS_UPLOAD_REQ Event d) TM_AWAIT(2000);wait 2 sec e) MTU_DS_CheckDownload;returns "download sequence performed" f) Evaluation 1) g) MTU_DS_CheckParameter(PARSET1);returns "PARSET1 active" h) Evaluation 2) i) SMI_DSToParServ;returns "DS_Data" (uploaded) j) Evaluation 3)

Test parameter –

Post condition / next test – 2718

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "download sequence performed"

2. Check "PARSET1 active"

2. Check ArgBlock "DS_Data"

Test passed "download sequence performed" = TRUE, and "PARSET1 active" = TRUE, and DataStorageObject = PARSET1

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2719

2720

### Test Specification © IO-Link – 375 – Version V1.1.5

### 2721 9.14.9 Upload request Event in OPERATE (Backup + Restore inactive)

### 2722 Table 304 defines the test conditions for this test case.

### 2723 Table 304 – Upload request Event in OPERATE (Backup + Restore inactive)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0361

Name TCM_ALIC_STOR_UPLOADREQEVENTOPERBACKRESTINACT

Purpose (short) Detection of upload request Event in OPERATE and Backup + Restore inactive

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Table E.3

Configuration / setup Master-Tester-System with MTU 2724

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) MTU generates a hidden "DS_UPLOAD_REQ" Event while in OPERATE. Master Port configuration is improper for DS (PortMode = IOL_AUTOSTART and PortMode = IOL_MANUAL) and Validation&Backup = "0" or "2"). Master does not show upload or download activities.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) TM_MASTER_UPLOAD(PARSET1);prepare EUT and MTU for identical DS data b) SMI_PortConfiguration (<ConfigList>);improper configuration for DS c) TM_AWAIT_PORT_STATUS (OPERATE) d) MTU_Set_ParameterSet(PARSET2);MTU activates PARSET2 e) MTU_DS_SetUpload(Event);MTU generates DS_UPLOAD_REQ Event f) TM_AWAIT(2000);wait 2 sec g) MTU_DS_CheckUpload;returns "upload sequence performed" h) Evaluation 1) i) MTU_DS_CheckDownload;returns "download sequence performed" j) Evaluation 2) k) SMI_DSToParServ;returns "DS_Data" (uploaded) l) Evaluation 3) m) repeat from b) with next “ConfigList”

Test parameter ConfigList = {APBS_NOTYPE_CHECK, ABPS_TYPE_COMP(<Validation&Backup = "0">), ABPS_TYPE_COMP(<VendorID = 0xFDE8>, <DeviceID = 0x002BD2>, <Validation&Backup = "2">)}

Post condition / next test – 2725

TEST CASE RESULTS CHECK / REACTION

Evaluation For each ConfigList entry

1. Check "upload sequence performed"

2. Check "download sequence performed"

3. Check ArgBlock "DS_Data"

Test passed For each ConfigList entry "upload sequence performed" = FALSE, and "download sequence performed" = FALSE, and DataStorageObject = no data/invalid (Header = "0") and length = 12 octets

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2726

2727

### Version V1.1.5 – 376 – Test Specification © IO-Link

### 2728 9.14.10 Start-up with empty DS (DS deactivated)

### 2729 Table 305 defines the test conditions for this test case.

### 2730 Table 305 – Start-up with empty DS (DS deactivated)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0362

Name TCM_ALIC_STOR_STARTUPEMPTDSDEACTIV

Purpose (short) Start-up with empty/invalid DS data object and deactivated Data Storage

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Table E.3

Configuration / setup Master-Tester-System with MTU 2731

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) For Port start-up an empty or invalid DS data object in Master is assumed as well as two different improper Port configurations. No upload takes place in both Test Procedure Loops: TPL1: PortMode = IOL_AUTOSTART (no type compatibility check) TPL2: PortMode = IOL_MANUAL (no Backup, no Restore)

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "config" b) MTU_DS_SetParameter(PARSET2);MTU activates PARSET2 c) MTU_DS_SetUpload(Flag_ON);set DS_UPLOAD_FLAG d) SMI_PortConfiguration(<config>) e) TM_AWAIT_PORT_STATUS(OPERATE) f) MTU_DS_CheckDSCommands;returns "DS_Commands performed" g) Evaluation 1) h) SMI_DSToParServ;returns "DS_Data" i) Evaluation 2) j) Repeat from b) with next "config"

Test parameter config = {APBS_NOTYPE_CHECK, ABPS_TYPE_COMP(< Backup&Restore = "2">)}

Post condition / next test – 2732

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "DS_Commands performed"

2. Check ArgBlock "DS_Data"

Test passed For all TPL: "DS_Commands performed" = FALSE, and DataStorageObject = no data/invalid (Header = "0") and length = 12 octets

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2733

2734

### Test Specification © IO-Link – 377 – Version V1.1.5

### 2735 9.14.11 Start-up with empty DS (DS activated – DS Upload)

### 2736 Table 306 defines the test conditions for this test case.

### 2737 Table 306 – Start-up with empty DS (DS activated – DS Upload)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0363

Name TCM_ALIC_STOR_STRTUPEMPTYDSDSACTIV

Purpose (short) Start-up with empty/invalid DS data object and activated Data Storage

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Table E.3

Configuration / setup Master-Tester-System with MTU 2738

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) For Port start-up an empty or invalid DS data object in Master is assumed as well as four different proper Port configurations. Upload takes place in all four Test Procedure Loops, where PortMode = IOL_MANUAL: TPL1: Validation&Backup = "3", DS_UPLOAD_FLAG = "0";Backup+Restore TPL2: Validation&Backup = "3", DS_UPLOAD_FLAG = "1";Backup+Restore TPL3: Validation&Backup = "4", DS_UPLOAD_FLAG = "0";Restore TPL4: Validation&Backup = "4", DS_UPLOAD_FLAG = "1";Restore

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "config", "flag" b) MTU_DS_SetParameter(PARSET2);MTU activates PARSET2 c) MTU_DS_SetUpload(<flag>);Test parameter d) SMI_PortConfiguration(ABPS_TYPE_COMP, <Validation&Backup = "config">);Test parameter e) TM_AWAIT_PORT_STATUS(OPERATE) f) MTU_DS_CheckUpload;returns "upload sequence performed" g) Evaluation 1) h) SMI_DSToParServ;returns "DS_Data" i) Evaluation 2) j) Repeat from b) with next "config", "flag"

Test parameter config = {3, 3, 4, 4};Type compatiple Device V1.1 (Backup+Restore or Restore) flag = {Flag_off, Flag_on, Flag_off, Flag_on};DS_UPLOAD_FLAG

Post condition / next test – 2739

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "upload sequence performed"

2. Check ArgBlock "DS_Data"

Test passed For all TPL: "upload sequence performed" = TRUE, and DataStorageObject = PARSET2

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2740

2741

### Version V1.1.5 – 378 – Test Specification © IO-Link

### 2742 9.14.12 Start-up with stored DS (Device replacement – DS Download)

### 2743 Table 307 defines the test conditions for this test case.

### 2744 Table 307 – Start-up with stored DS (Device replacement – DS Download)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0364

Name TCM_ALIC_STOR_STRTUPDEVREPLACEDSDOWN

Purpose (short) Start-up after Device replacement with DS Download (checksum mismatch)

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Table E.3

Configuration / setup Master-Tester-System with MTU 2745

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) For Port start-up a stored or valid DS data object in Master is assumed as well as three different proper Port configurations. Download takes place in all three Test Procedure Loops, where PortMode = IOL_MANUAL, DS = activated, and DS checksum = mismatch: TPL1: Validation&Backup = "3", DS_UPLOAD_FLAG = "0";Backup+Restore TPL2: Validation&Backup = "4", DS_UPLOAD_FLAG = "0";Restore TPL3: Validation&Backup = "4", DS_UPLOAD_FLAG = "1";Restore

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "config", "flag" b) MTU_Set_Parameter(PARSET1);MTU activates PARSET1 c) SMI_PortConfiguration(ABPS_TYPE_COMP, <Validation&Backup = "config">);Test parameter d) TM_AWAIT_PORT_STATUS(OPERATE) e) SMI_DSToParServ;returns DataStorageObject f) MTU_State_Deactivate;disable response to Master request g) MTU_DS_SetParameter(PARSET2);MTU activates PARSET2 h) MTU_DS_SetUpload(<flag>);Test parameter i) MTU_ State_Activate;enable response to Master request j) TM_AWAIT_PORT_STATUS(OPERATE) k) MTU_DS_CheckDownload;returns "download sequence performed" l) Evaluation 1 m) MTU_DS_CheckParameter(PARSET1);returns "PARSET1 active" n) Evaluation 2 o) Repeat from b) with next "config", " flag"

Test parameter config = {3, 4, 4};Type compatiple Device V1.1 (Backup+Restore or Restore) flag = {Flag_off, Flag_off, Flag_on};DS_UPLOAD_FLAG

Post condition / next test – 2746

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL

1. Check "download sequence performed"

2. Check "PARSET1 active"

Test passed For all TPL "download sequence performed" = TRUE, and "PARSET1 active" = TRUE

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2747

2748

### Test Specification © IO-Link – 379 – Version V1.1.5

### 2749 9.14.13 Start-up with stored DS (Device replacement – no DS Download)

### 2750 Table 308 defines the test conditions for this test case.

### 2751 Table 308 – Start-up with stored DS (Device replacement – no DS Download)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0365

Name TCM_ALIC_STOR_STRTUPDEVREPLACENODSDOWN

Purpose (short) Start-up after Device replacement without DS Download (checksum match)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Table E.3

Configuration / setup Master-Tester-System with MTU 2752

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) For Port start-up a stored or valid DS data object in Master is assumed and the Device is replaced by a type compatible one. No Download takes place in all four Test Procedure Loops: TPL1: PortMode = IOL_MANUAL, Validation&Backup = "3";Backup+Restore TPL2: PortMode = IOL_MANUAL, Validation&Backup = "4";Restore TPL3: PortMode = IOL_AUTOSTART;no type compatibility check TPL4: PortMode = IOL_MANUAL, Validation&Backup = "2";no Backup, no Restore

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "config" b) MTU_Set_Parameter(PARSET1);MTU activates PARSET1 c) SMI_PortConfiguration(<config>);Test parameter d) TM_AWAIT_PORT_STATUS(OPERATE) e) MTU_State_Deactivate;disable response to Master request f) MTU_State_Activate;enable response to Master request g) TM_AWAIT_PORT_STATUS(OPERATE) h) MTU_DS_CheckDSComands;returns "DS_Commands performed" i) Evaluation 1 j) MTU_DS_CheckParameter(PARSET1);returns "PARSET1 active" k) Evaluation 2 l) Repeat from b) with next "config"

Test parameter config= {ABPS_TYPE_COMP, <Validation&Backup = "3">, ABPS_TYPE_COMP, <Validation&Backup = "4">, APBS_NO_TYPE_CHECK, ABPS_TYPE_COMP, <Validation&Backup = "2">}

Post condition / next test – 2753

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "DS_Commands performed "

2. Check "PARSET1 active"

Test passed "DS_Commands performed" = FALSE, and "PARSET1 active" = TRUE

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2754

2755

### Version V1.1.5 – 380 – Test Specification © IO-Link

### 2756 9.14.14 Start-up with stored DS (Device replacement – DS Upload)

### 2757 Table 309 defines the test conditions for this test case.

### 2758 Table 309 – Start-up with stored DS (Device replacement – DS Upload)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0366

Name TCM_ALIC_STOR_STRTUPDEVREPLACEDSUPFLAG

Purpose (short) Start-up after Device replacement with raised DS upload flag

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4

Configuration / setup Master-Tester-System with MTU 2759

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) For Port start-up a stored or valid DS data object in Master is assumed and the Device is replaced by a type compatible one setting DS_UPLOAD_FLAG. Upload takes place at proper PortConfiguration (PortMode = IOL_MANUAL, Validation&Backup = "3")

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_DS_SetParameter(PARSET1);MTU activates PARSET1 b) SMI_PortConfiguration(ABPS_TYPE_COMP, Validation&Backup = "3") c) TM_AWAIT_PORT_STATUS(OPERATE) d) MTU_State_Deactivate;disable response to Master request e) MTU_DS_SetParameter(PARSET2);MTU activates PARSET2 f) MTU_DS_SetUpload(<Flag_on>);set DS_UPLOAD_FLAG g) f) MTU_State_Activate;enable response to Master request h) TM_AWAIT _PORT_STATUS(OPERATE) i) MTU_DS_CheckUpload;returns "upload sequence performed" j) Evaluation 1) k) SMI_DSToParServ;returns "DS_Data" l) Evaluation 2)

Test parameter –

Post condition / next test – 2760

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "upload sequence performed"

2. Check ArgBlock "DS_Data"

Test passed "upload sequence performed" = TRUE, and DataStorageObject = PARSET2

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2761

2762

### Test Specification © IO-Link – 381 – Version V1.1.5

### 2763 9.14.15 Start-up with stored DS (Device replacement – no DS Upload)

### 2764 Table 310 defines the test conditions for this test case.

### 2765 Table 310 – Start-up with stored DS (Device replacement – no DS Upload)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0367

Name TCM_ALIC_STOR_STRTUPDEVREPLACEDSFLAGNOUPLOAD

Purpose (short) Start-up after Device replacement with raised DS upload flag – DS upload blocked

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4

Configuration / setup Master-Tester-System with MTU 2766

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) For Port start-up a stored or valid DS data object in Master is assumed and the Device is replaced by a type compatible one setting DS_UPLOAD_FLAG. No Upload takes place in all three Test Procedure Loops: TPL1: PortMode = IOL_MANUAL, Validation&Backup = "4";Restore TPL2: PortMode = IOL_MANUAL, Validation&Backup = "2";no Backup, no Restore TPL3: PortMode = IOL_AUTOSTART;no type compatibility check

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) Assign first value to "config" b) MTU_DS_SetParameter(PARSET1);MTU activates PARSET1 c) SMI_PortConfiguration(<config>);Test parameter d) TM_AWAIT_PORT_STATUS(OPERATE) e) MTU_State_Deactivate;disable response to Master request f) MTU_DS_SetParameter(PARSET2);MTU activates PARSET2 g) MTU_DS_SetUpload(<Flag_on>);set DS_UPLOAD_FLAG h) f) MTU_State_Activate;enable response to Master request i) TM_AWAIT_PORT_STATUS(OPERATE) j) MTU_DS_CheckUpload;returns "upload sequence performed" k) Evaluation 1) l) SMI_DSToParServ;returns "DS_Data" m) Evaluation 2) n) Repeat from b) with next "config"

Test parameter config = {ABPS_TYPE_COMP, <Validation&Backup = "4">, ABPS_TYPE_COMP, <Validation&Backup = "2">, APBS_NO_TYPE_CHECK}

Post condition / next test – 2767

TEST CASE RESULTS CHECK / REACTION

Evaluation For all TPL:

1. Check "upload sequence performed"

2. Check ArgBlock "DS_Data"

Test passed For all TPL: "upload sequence performed" = FALSE, and DataStorageObject ≠ PARSET2

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2768

2769

### Version V1.1.5 – 382 – Test Specification © IO-Link

### 2770 9.14.16 Corrupted DS Index_List

### 2771 Table 311 defines the test conditions for this test case.

### 2772 Table 311 – Corrupted DS Index_List

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0368

Name TCM_ALIC_STOR_DSINDLISTCORRUPT

Purpose (short) DS error in case of read Index_List fault during Port start-up

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Annex E.16

Configuration / setup Master-Tester-System with MTU 2773

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) An error occurs while reading an Index_List during Port start-up. This leads to the following consequences:

- No Upload/Download occurs (user view)

- MTU is on hold in PREOPERATE

- PortEvent is generated

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) TM_MASTER_UPLOAD(PARSET1);PARSET1 uploaded to Master DS b) DLL_GetPortEvents;clear Event entries c) MTU_State_Deactivate;disable response to Master request d) MTU_DS_SetParameter(PARSET2);MTU activates PARSET2 e) MTU_DS_SetUpload(<Flag_on>);set DS_UPLOAD_FLAG f) MTU_DS_SetError(IndexList_err);MTU falsifies Index_List g) MTU_State_Activate;enable response to Master request h) DLL_GetPortEvents(3);minED=3, returns "PortEventList i) Evaluation 1) j) MTU_DS_CheckUpload;returns "upload sequence performed" k) Evaluation 2) l) MTU_State_CheckPreoperate;returns "PREOPERATE" m) Evaluation 3) n) SMI_PortStatus;returns "PortStatusList" o) Evaluation 4) p) SMI_PortConfiguration(ABPS_PORTTODI);switch Port to DI mode q) DLL_GetPortEvents(1);minED=1, returns "PortEventList r) Evaluation 5) s) SMI_PortStatus;returns "PortStatusList" t) Evaluation 6)

Test parameter –

Post condition / next test – 2774

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ArgBlock "PortEventList"

2. Check "upload sequence performed"

3. Check "PREOPERATE"

4. Check ArgBlock "PortStatusList"

5. Check ArgBlock "PortEventList"

6. Check ArgBlock "PortStatusList"

### Test Specification © IO-Link – 383 – Version V1.1.5

TEST CASE RESULTS CHECK / REACTION

Test passed PortEventList.Entry1 (EventQualifier: Master/Port, Error, Event appears, EventCode = 0x180C), and "upload sequence performed" = FALSE, and "PREOERATE" = REACHED, and PortStatusList: PortStatusInfo = PORT_DIAG, DiagEntryx = result of 1), and PortEventList.Entry1 (EventQualifier: Master/Port, Error ,Event disappears, EventCode = 0x180B), and PortStatusList: PortStatusInfo = DI, DiagEntryx = empty

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2775

2776

### Version V1.1.5 – 384 – Test Specification © IO-Link

### 2777 9.14.17 DS Download fault

### 2778 Table 312 defines the test conditions for this test case.

### 2779 Table 312 – DS Download fault

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0369

Name TCM_ALIC_STOR_DSDOWNLOADFAULT

Purpose (short) DS error during DS download at Port start-up

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4, Annex E.16

Configuration / setup Master-Tester-System with MTU 2780

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) An error occurs while downloading DS data object during Port start-up, for example a read Index returns negative response. This leads to Download aborted, MTU on hold in PREOPERATE, PortEvent generated, rollback of DS data object.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) TM_MASTER_UPLOAD(PARSET1;PARSET1 uploaded to Master DS b) DLL_GetPortEvents;clear Event entries c) MTU_State_Deactivate;disable response to Master request d) MTU_DS_SetParameter(PARSET2);MTU activates PARSET2 e) MTU_DS_SetError(W_Index19_err);MTU to return negative response f) MTU_State_Activate;enable response to Master request g) MTU_DS_WAIT(DS_BREAK);wait until Download aborted h) Evaluation 1) i) DLL_GetPortEvents(3);minED=3, returns"PortEventList

j) Evaluation 2) k) MTU_DS_CheckParameter(PARSET1);returns "PARSET1 active" l) Evaluation 3) m) MTU_State_CheckPreoperate;returns "PREOPERATE" n) Evaluation 4) o) SMI_PortStatus;returns "PortStatusList" p) Evaluation 5) q) SMI_PortConfiguration(ABPS_PORTTODI);switch Port to DI mode r) DLL_GetPortEvents(1);minED=1, returns "PortEventList s) Evaluation 6) t) SMI_PortStatus;returns "PortStatusList" u) Evaluation 7)

Test parameter –

Post condition / next test – 2781

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check DS_BREAK

2. Check ArgBlock "PortEventList"

3. Check "PARSET1 active"

4. Check "PREOPERATE"

5. Check ArgBlock "PortStatusList"

6. Check ArgBlock "PortEventList"

7. Check ArgBlock "PortStatusList"

Test passed DS_BREAK detected, and PortEventList.Entry1 (EventQualifier: Master/Port, Error, Event appears, EventCode = 0x180D, and "PARSET1 active" = TRUE, and "PREOPERATE" = REACHED, and PortStatusList: PortStatusInfo = PORT_DIAG, DiagEntryx = result of 1), and PortEventList.Entry1 (EventQualifier: Master/Port, Error, Event disappears, EventCode = 0x180D), and

### Test Specification © IO-Link – 385 – Version V1.1.5

TEST CASE RESULTS CHECK / REACTION PortStatusList: PortStatusInfo = DI, DiagEntryx = empty

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2782

### Version V1.1.5 – 386 – Test Specification © IO-Link

### 2783 9.14.18 DS Upload fault

### 2784 Table 313 defines the test conditions for this test case.

### 2785 Table 313 – DS Upload fault

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0370

Name TCM_ALIC_STOR_DSUPLOADFAULT

Purpose (short) DS error during DS Upload at Port start-up

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4

Configuration / setup Master-Tester-System with MTU 2786

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) An error occurs while uploading DS data object during Port start-up, for example a read Index returns negative response. This leads to Upload aborted, MTU on hold in PREOPERATE, PortEvent generated, rollback of DS data object.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) TM_MASTER_UPLOAD(PARSET1);PARSET1 uploaded to Master DS b) DLL_GetPortEvents;clear Event entries c) MTU_State_Deactivate;disable response to Master request d) MTU_DS_SetParameter(PARSET2);MTU activates PARSET2 e) MTU_DS_SetUpload(<Flag_on>);set DS_UPLOAD_FLAG f) MTU_DS_SetError(R_Index19_err);MTU to return negative response g) MTU_State_Activate;enable response to Master request h) MTU_DS_WAIT(DS_BREAK);wait until Download aborted i) Evaluation 1) j) DLL_GetPortEvents(3);minED=3, returns"PortEventList

k) Evaluation 2) l) SMI_DSToParServ;returns "DS_Data" m) Evaluation 3) n) MTU_State_CheckPreoperate;returns "PREOPERATE" o) Evaluation 4) p) SMI_PortStatus;returns "PortStatusList" q) Evaluation 5) r) SMI_PortConfiguration(ABPS_PORTTODI);switch Port to DI mode s) DLL_GetPortEvents(1);minED=1 , returns"PortEventList t) Evaluation 6) u) SMI_PortStatus;returns "PortStatusList" v) Evaluation 7)

Test parameter –

Post condition / next test – 2787

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check DS_BREAK

2. Check ArgBlock "PortEventList"

3. Check ArgBlock "DS_Data"

4. Check "PREOPERATE"

5. Check ArgBlock "PortStatusList"

6. Check ArgBlock "PortEventList"

7. Check ArgBlock "PortStatusList"

### Test Specification © IO-Link – 387 – Version V1.1.5

TEST CASE RESULTS CHECK / REACTION

Test passed DS_BREAK detected, and PortEventList.Entry1 (EventQualifier: Master/Port, Error, Event appears, EventCode = 0x180C), and DataStorageObject = PARSET1, and "PREOPERATE" = REACHED, and PortStatusList: PortStatusInfo = PORT_DIAG, DiagEntryx = result of 1), and PortEventList.Entry1 (EventQualifier: Master/Port, Error,…, MODE = Event disappears, …, EventCode = 0x180C), and PortStatusList: PortStatusInfo = DI, DiagEntryx = empty

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2788

### Version V1.1.5 – 388 – Test Specification © IO-Link

### 2789 9.14.19 Incompatible Device and DS

### 2790 Table 314 defines the test conditions for this test case.

### 2791 Table 314 – Incompatible Device and DS

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0371

Name TCM_ALIC_STOR_INCOMPDEVICE&DS

Purpose (short) DS in case of identification fault (incompatible Device)

Equipment under test (EUT) Master + Port

Test case version 1.1

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4

Configuration / setup Master-Tester-System with MTU 2792

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) At start-up, the Master detects a Device, not type compatible with configured data. This leads to PortEvent generated, no DS Upload/Download, MTU is on hold in PREOPERATE and shows diagnosis information.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) TM_MASTER_UPLOAD(PARSET1);PARSET1 uploaded to Master DS b) DLL_GetPortEvents;clear Event entries c) MTU_State_Deactivate;disable response to Master request d) MTU_ DPP1_Set(DeviceID = 0x102BD2);MTU gets different DeviceID e) MTU_DS_SetParameter(PARSET2);MTU activates PARSET2 f) MTU_DS_SetUpload(<Flag_on>);set DS_UPLOAD_FLAG g) MTU_State_Activate;enable response to Master request h) DLL_GetPortEvents(3);minED=3, returns"PortEventList

i) Evaluation 1) j) MTU_DS_CheckDSCommands;;returns "DS_Command performed" k)Evaluation 2) l) SMI_PortStatus;returns "PortStatusList" m) Evaluation 3) n) SMI_DSToParServ;returns "DS_Data" o) Evaluation 4) p) SMI_PortConfiguration(ABPS_PORTTODI);switch Port to DI mode q) DLL_GetPortEvents(1);minED=1 , returns"PortEventList r) Evaluation 5) s) TM_AWAIT_PORT_STATUS(DI);returns "PortStatusList" t) Evaluation 6)

Test parameter –

Post condition / next test – 2793

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ArgBlock "PortEventList"

2. Check "DS_Command"

3. Check ArgBlock "PortStatusList"

4. Check ArgBlock "DS_Data"

5. Check ArgBlock "PortEventList"

6. Check ArgBlock "PortStatusList"

Test passed PortEventList.Entry1 EventQualifier: Master/Port, Error, Event appears, EventCode = 0x1803), and "DS_Commands performed" = FALSE, and PortStatusList: PortStatusInfo = PORT_DIAG, DiagEntryx = result of 1), and DataStorageObject = PARSET1, and PortEventList.Entry1 (EventQualifier: : Master/Port, Error, Event disappears,EventCode = 0x1803, and PortStatusList: PortStatusInfo = DI, DiagEntryx = empty

### Test Specification © IO-Link – 389 – Version V1.1.5

TEST CASE RESULTS CHECK / REACTION

Test not passed (examples) Evaluation 1,2,3,4,5,6 failed

Report All evaluations <ok | nok>

2794

### Version V1.1.5 – 390 – Test Specification © IO-Link

### 2795 9.14.20 Master power OFF/ON (non-volatile DS data)

### 2796 Table 315 defines the test conditions for this test case.

### 2797 Table 315 – Master power OFF/ON (non-volatile DS data)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0372

Name TCM_ALIC_STOR_POWEROFFONNONVOLATILEDS

Purpose (short) Non-volatile storage of Port configuration and DS data

Equipment under test (EUT) Master + Port

Test case version 1.0

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.3, B.2.3, B.2.4

Configuration / setup Master-Tester-System with MTU 2798

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) After power off/on, the Master starts with the same configuration data and DS data as before the power cycle. These data are saved in non-volatile memory.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) TM_MASTER_UPLOAD (PARSET1);PARSET1 uploaded to Master DS b) Switch off EUT power supply;Depending on Tester implementation c) Switch on EUT power supply d) TM_AWAIT_PORT_STATUS (OPERATE) e) SMI_DSToParServ;returns "DS_Data" f) Evaluation 1)

Test parameter –

Post condition / next test – 2799

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ArgBlock "DS_Data"

Test passed DataStorageObject = PARSET1

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2800

2801

### Test Specification © IO-Link – 391 – Version V1.1.5

### 2802 9.14.21 DS Upload trial with locked Device Data Storage

### 2803 Table 316 defines the test conditions for this test case.

### 2804 Table 316 – DS Upload trial with locked Device Data Storage

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0280

Name TCM_ALIC_STOR_DSLOCKED

Purpose (short) DS Upload trial with locked Device Data Storage

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master DS test: test to pass

Specification (clause) [1], 11.2, 11.4, B.2.3, B.2.4

Configuration / setup Master-Tester-System with MTU 2805

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master shall detect the locked Data Storage of a Device. In this case the Master shall deny access for all data storage actions from its own data storage handler.

NOTE Even it is highly recommended for Devices not to implement locking for DS, Devices in the field can show this feature and Master shall be able to handle it.

Precondition EUT: EUT_PORT_DI MTU: MTU_STANDARD_STATE

Procedure a) MTU_DS_Locked b) DLL_GetPortEvents;clear Event entries c) SMI_PortConfiguration(ABPS_TYPE_COMP <Validation&Backup = "3">) d) TM_AWAIT_PORT_STATUS (PORT_DIAG) e) MTU_DS_CheckDSCommands;returns "DS_Command performed" f) Evaluation 1) g) DLL_GetPortEvents(1);minED=1, returns "PortEventList h) TM_AWAIT_PORT_EVENT i) Evaluation 2) j) SMI_DSToParServ;returns "DS_Data" k) Evaluation 3)

Test parameter –

Post condition / next test – 2806

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check "DS_Command performed"

2. Check ArgBlock "PortEvent"

3. Check "DS_Data"

Test passed "DS_Command performed" = FALSE, and PortEventList.Entry1 (EventQualifier: INSTANCE = SYS, SOURCE = Master/Port, TYPE = Error, Mode = Event appears, EventCode = 0x180B), and "DS_Data" = no data/invalid (Header = "0") and length = 12 octets

Test not passed (examples) Any evaluation failed

Report All evaluations <ok | nok>

2807

2808

2809

### Version V1.1.5 – 392 – Test Specification © IO-Link

### 2810 9.15 Legacy Device ("V1.0")

### 2811 9.15.1 General

### 2812 Since a Master designed according to [1] shall support legacy Devices designed according to

### 2813 [5], it shall pass the following test cases.

### 2814 9.15.2 Master detects legacy Device and establishes connection

### 2815 Table 317 defines the test conditions for this test case.

### 2816 Table 317 – Master detects legacy Device and establishes connection

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0285

Name TCM_LGCY_MANY_DETECTANDCONNECT

Purpose (short) Master detects legacy Device and establishes connection

Equipment under test (EUT) Master + Port

Test case version 1.3

Category / type Master legacy Device test: test to pass

Specification (clause) [5], 7.2.2.1, 9.3.3; [1], 9.2.3.2, A.2.6, Figures 71, 72

Configuration / setup Master-Tester-Unit 2817

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Compatibility of the startup phase of V1.1 Master and a V1.0 Device. Master shall detect connection to a V1.0 Device and shall adjust its startup behavior.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_INACTIVE

Procedure a) MTU_DPP1_Set(RevisionID = 0x10) b) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) c) TM_AWAIT_PORT_STATUS(OPERATE);returns "PortStatusList" d) Evaluation 1) e) MTU_Startup_Check10;returns "Startup1.0" f) Evaluation 2)

Test parameter –

Post condition /next test – 2818

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ArgBlock "PortStatusList"

2. Check "Startup1.0"

Test passed PortStatusList: PortStatusInfo = OPERATE, TransmissionRate = COM2, MasterCycleTime (decoded) ≥ 4 ms, InputDataLength = 1, OutputDataLength = 1, VendorID = 0xFDE8, DeviceID = 0x002BD2, RevisionID = 0x10, and Startup1.0 = TRUE

Test not passed (examples) Any evaluation failed, or Test_Timeout

Report Start-up according to [5]: <yes/no> <ok | nok>

2819

2820

### Test Specification © IO-Link – 393 – Version V1.1.5

### 2821 9.15.3 Master detects legacy Device and establishes interleave mode

### 2822 Table 318 defines the test conditions for this test case.

### 2823 Table 318 – Master detects legacy Device and establishes interleave mode

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0286

Name TCM_LGCY_MANY_DETECTANDINTERLEAVE

Purpose (short) Master detects legacy Device and establishes interleave mode

Equipment under test (EUT) Master + Port

Test case version 1.3

Category / type Master legacy Device test: test to pass

Specification (clause) [5], 9.3.3; [1], 9.2.3.5, A.2.6, Figures 71, 72

Configuration / setup Master-Tester-Unit 2824

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Compatibility of the startup phase of V1.1 Master and a V1.0 Device using M-sequence TYPE_1 in interleave mode. Master shall detect connection to a V1.0 Device and shall adjust its startup behavior.

Precondition MTU: MTU_STANDARD_STATE EUT: PORT_INACTIVE

Procedure a) MTU_DPP1_Set(RevisionID = 0x10) b) MTU_DPP1_Set(InputLength = 3) c) MTU_DPP1_Set(OutputLength = 3) d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) e) TM_AWAIT_PORT_STATUS(OPERATE);returns "PortStatusList" f) Evaluation 1) g) MTU_Startup_Check10;returns "Startup1.0" h) Evaluation 2)

Test parameter –

Post condition / next test – 2825

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ArgBlock "PortStatusList"

2. Check "Startup1.0"

Test passed PortStatusList: PortStatusInfo = OPERATE, TransmissionRate = COM2, MasterCycleTime (decoded) ≥ 4 ms, InputDataLength = 3, OutputDataLength = 3, VendorID = 0xFDE8, DeviceID = 0x002BD2, RevisionID = 0x10, and Startup1.0 = TRUE

Test not passed (examples) Any evaluation failed, or Test_Timeout

Report Start-up according to [5]: <yes/no> <ok | nok> M-sequence TYPE_1 used: <yes/no> <ok | nok>

2826

2827

### Version V1.1.5 – 394 – Test Specification © IO-Link

### 2828 9.15.4 Master receives an Event without details (Warning)

### 2829 Table 319 defines the test conditions for this test case.

### 2830 Table 319 – Master receives an Event without details (Warning)

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0287

Name TCM_ALIC_EVNT_NODETAILSWARNING

Purpose (short) Master receives an Event without details

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master legacy Device test: test to pass

Specification (clause) [5], 7.2.4.4.1 [1], 7.3.8.3, 8.3.3.1, 11.5, Annex A.6, Annex D

Configuration / setup Master-Tester-Unit 2831

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master receives Event without details (StatusCode type 1, notification). Master extracts the EventCode (type 1), maps it accordingly into StatusCode (type 2) and transfers it to the upper-level system. Similarly, the StatusCode indicates with 1 bit the validity of the Process Data. The Master shall read this information as soon as possible to indicate the actual state of the Process Data to the upper system. The Master acknowledges the Event; the Master-Tester-Unit resets the Event flag.

Precondition MTU: MTU_STANDARD_STATE, No Event in process EUT: PORT_INACTIVE

Procedure a) MTU_DPP1_Set(RevisionID = 0x10) b) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) c) TM_AWAIT_PORT_STATUS(OPERATE);returns "PortStatusList" d) Evaluation 1) e) MTU_Event_SetStatusCode(0x01) f) MTU_Event_TriggerAndWaitForAck;returns "EventAck" j) Evaluation 2) g) DLL_GetDeviceEvents(1);minED = 1, returns "EventList" h) Evaluation 3) k) TM_AWAIT_PORT_STATUS(OPERATE);returns "PortStatusList" l) Evaluation 4)

Test parameter –

Post condition / next test – 2832

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ArgBlock "PortStatusList"

2. Check "EventAck"

3. Check "EventList"

4. Check ArgBlock "PortStatusList"

Test passed PortStatusList: PortStatusInfo = OPERATE, RevisionID = 0x10, NumberOfDiags = 0, and EventAck: StatusCodeRead = TRUE, AllSlotsRead = TRUE, Ack. = TRUE, and EventList.Entry1 = 0xFF80, Application, notification, and PortStatusList: PortStatusInfo = OPERATE, RevisionID = 0x10, NumberOfDiags = 0;since notifications will not be placed into list

Test not passed (examples) Any evaluation failed, or Test-Timeout

Report Propagated information to the upper-level system: <code> <ok | nok> Master acknowledgement: <code> <ok | nok> Event flag: <0/1> <ok | nok>

2833

### Test Specification © IO-Link – 395 – Version V1.1.5

### 2834 9.15.5 ISDU Write interrupted by an Event leads to a Write error

### 2835 Table 320 defines the test conditions for this test case.

### 2836 Table 320 – ISDU Write interrupted by an Event leads to a Write error

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0289

Name TCM_LGCY_MANY_EVENTINTERRUPTSISDU

Purpose (short) ISDU Write interrupted by an Event leads to write error

Equipment under test (EUT) Master + Port

Test case version 1.3

Category / type Master legacy Device test: test to pass

Specification (clause) [5], 7.3.6.3, Figure 47; [1], Table C.2, Annex C.3.8

Configuration / setup Master-Tester-Unit 2837

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) According to [5], the legacy Device can cancel an ongoing ISDU transfer via an Event. This Event shall lead to a Read error or Write error on the Master side.

Precondition MTU: MTU_STANDARD_STATE, no Event pending EUT: PORT_INACTIVE

Procedure a) MTU_DPP1_Set(RevisionID = 0x10) b) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) c) TM_AWAIT_PORT_STATUS(OPERATE);returns "PortStatusList" d) Evaluation 1) e) MTU_Add_ISDU(Index = <Index>, Subindex = 0, Specialty = ISSUE_EVENT) f) SMI_DeviceWrite(ABPS_DEVICEWRITE(<Index>));returns "JobError" ;the Master initiates an ISDU Write with too large length of data. The legacy ;Device sends an Event (DL, Error, Event single shot, EventCode = 0x5200) ;during the ISDU service to abort it. g) Evaluation 2) h) TM_AWAIT_PORT_STATUS(OPERATE);returns "PortStatusList" i) Evaluation 3)

Test parameter <Index> = 251, data = all "0", data length = 33

Post condition / next test – 2838

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ArgBlock "PortStatusList"

2. Check ArgBlock "JobError"

3. Check ArgBlock "PortStatusList"

Test passed PortStatusList: PortStatusInfo = OPERATE, RevisionID = 0x10, NumberOfDiags = 0, and JobError: ErrorCode = 0x80, AdditionalCode = 0x23, and PortStatusList: PortStatusInfo = OPERATE, RevisionID = 0x10, NumberOfDiags = 0

Test not passed (examples) Any evaluation failed, or Test_Timeout, or Receive "JobError" does not terminate within 10 s, or Received positive result

Report Event received by the upper-level system: <yes/no> <ok | nok> ISDU service aborted: <yes/no> <ok | nok>

2839

2840

### Version V1.1.5 – 396 – Test Specification © IO-Link

### 2841 9.15.6 Master transforms PD_invalid Event into appropriate propagation

### 2842 Table 321 defines the test conditions for this test case.

### 2843 Table 321 – Master transforms PD_invalid Event into appropriate propagation

TEST CASE ATTRIBUTES IDENTIFICATION / REFERENCE

Identification (ID) SDCI_TC_0290

Name TCM_LGCY_MANY_PDINVALIDEVENT

Purpose (short) Master transforms PD_invalid and PD_valid Event into appropriate propagation

Equipment under test (EUT) Master + Port

Test case version 1.2

Category / type Master legacy Device test: test to pass

Specification (clause) [5], 7.2.4.4; [1], A.6.2, A.6.3

Configuration / setup Master-Tester-Unit 2844

TEST CASE CONDITIONS / PERFORMANCE

Purpose (detailed) Master transforms a "PD invalid" and a "PD_valid" Event from a legacy Device into an appropriate propagation form and passes Process Data correctly.

Precondition MTU: MTU_STANDARD_STATE, No Event in process EUT: PORT_INACTIVE

Procedure a) MTU_DPP1_Set(RevisionID = 0x10) b) MTU_DPP1_Set(PDIn length) = 0x83;”32” Bit c) MTU_DPP1_Set(PDOut length) = 0x83;”32” Bit d) SMI_PortConfiguration(ABPS_NOTYPE_CHECK) e) TM_AWAIT_PORT_STATUS(OPERATE);returns "PortStatusList" f) Evaluation 1) g) MTU_State_MirrorPD;perform PD mirroring h) MTU_State_SetPDValidity(VALID) i) TM_AWAIT_PD_VALIDITY(VALID);return "PDIn" j) Evaluation 2) k) SMI_PDOut(ABPS_PDOUT<OutputDataLength=4, PDO = [0x12,0x34,0x56,0x78]>) l) SMI_PDIn;returns "PDIn" m) Evaluation 3) n) MTU_State_SetPDValidity(INVALID);Device issues Event with PD Invalid o) TM_AWAIT_PD_VALIDITY(INVALID);return "PDIn" p) Evaluation 4) q) MTU_State_SetPDValidity(VALID);Device issues Event with PD Valid r) TM_AWAIT_PD_VALIDITY(VALID);return "PDIn" s) Evaluation 5)

Test parameter –

Post condition – 2845

TEST CASE RESULTS CHECK / REACTION

Evaluation 1) Check ArgBlock "PortStatusList"

2. Check ArgBlock "PDIn"

3. Check ArgBlock "PDIn"

4. Check ArgBlock "PDIn

5. Check ArgBlock "PDIn

Test passed PortStatusList: PortStatusInfo = OPERATE, RevisionID = 0x10, and PDIn: PQI.PQ = VALID, and PDIn: PDI = [0x12,0x34,0x56,0x78], and PDIn: PQI.PQ = INVALID, and PDIn: PQI.PQ = VALID

Test not passed (examples) Any evaluation failed, or Test_Timeout

Report Correct propagation of PD_INVALID: <yes/no> <ok | nok> Correct propagation of PD_VALID: <yes/no> <ok | nok> Event acknowledged: <yes/no> <ok | nok>

2846 2847

Test Specification © IO-Link – 397 – Version V1.1.5

2848 9.16 Test report template

2849 The template is defined by the Master-Tester. The test report shall present at least the reports 2850 of the test cases.

2851

Version V1.1.5 – 398 – Test Specification © IO-Link

2852 10 Environmental tests

2853 10.1 General

2854 Annex G in [1] defines the environmental tests (EMC) for the SDCI communication part of a 2855 Master/Device system. A passed EMC test is a precondition for a Manufacturer Declaration. It 2856 depends on the particular technology of a Device and the countries of deployment, whether 2857 additional environmental tests are necessary to achieve for example a CE mark for Europe.

2858 10.2 Product specific standards

2859 Usually, the product standard for a Master is the IEC 61131-2. For Devices, the major product 2860 standard is the IEC 60947-1.

2861 10.3 EMC tests

2862 EMC tests in respect to a particular phenomenon are defined in the IEC 61000-4-x series. De- 2863 tails for the execution are described in Annex G.2 in [1] and in 5.4 or 5.5 respectively.

2864 Hint: Length "L" in Figures G.4 and G.8 in [1] shall be as short as possible.

2865 10.4 Test report templates

2866 10.4.1 Overview

2867 Tests are required for the following phenomena:

2868 • Electrostatic discharge (ESD: IEC 61000-4-2)

2869 • Electromagnetic field (HF: IEC 61000-4-3)

2870 • Fast transients (Burst: IEC 61000-4-4)

2871 • Conducted radio frequency (CRF: IEC 61000-4-6)

2872 The SDCI manufacturer declaration of conformity comprises EMC tests according to Annex 2873 G.2.4 in [1]. The following forms or any other document may be used as long as it contains the 2874 same information.

2875

### Test Specification © IO-Link – 399 – Version V1.1.5

### 2876 10.4.2 ESD

### 2877 Figure 14 shows a proposed template for ESD tests.

Project:

Test Item:

Responsible Party:

Tester:

Applied Standard/Guideline:

Type of Device:

Test Location:

Time Range:

2878

Test Requirements/Results:

Type of discharge Requirement fulfilled? Achieved ImmunityTest Voltage kV PerformanceCriterion

yes no

Contact discharge

Air discharge

HCP

VCP

2879

Result:

Test requirements are fulfilled

not fulfilled

Every single requirement must be met.

2880

2881 Remarks:

2882 ______________________________________________________________________________________________________

2883 ______________________________________________________________________________________________________

2884 ______________________________________________________________________________________________________

2885 ______________________________________________________________________________________________________

2886 ______________________________________________________________________________________________________

2887

2888 Enclosures:

2889 ______________________________________________________________________________________________ 2890 ______________________________________________________________________________________________________

2891

2892

2893

2894 Date Tester’s Signature

2895

Version V1.1.5 – 400 – Test Specification © IO-Link

2896 Figure 14 – Proposed template for ESD tests

2897

### Test Specification © IO-Link – 401 – Version V1.1.5

### 2898 10.4.3 HF

### 2899 Figure 15 shows a proposed template for HF tests.

Project:

Test Item:

Responsible Party:

Tester:

Applied Standard/Guideline:

Type of Device:

Test Location:

Time Range:

2900

Test Requirements/Results:

Type of HF Field Requirement fulfilled? Achieved Immunity Test Field V/m Performance Criterion

yes no

2901

Result:

Test requirements are fulfilled

not fulfilled

Every single requirement must be met.

2902

2903 Remarks:

2904 ______________________________________________________________________________________________________

2905 ______________________________________________________________________________________________________

2906 ______________________________________________________________________________________________________

2907 ______________________________________________________________________________________________________

2908 ______________________________________________________________________________________________________

2909

2910 Enclosures:

2911 ______________________________________________________________________________________________ 2912 ______________________________________________________________________________________________________

2913

2914

2915

2916 Date Tester’s Signature

### 2917 Figure 15 – Proposed template for HF tests

Version V1.1.5 – 402 – Test Specification © IO-Link

2918

2919

### Test Specification © IO-Link – 403 – Version V1.1.5

### 2920 10.4.4 Burst

### 2921 Figure 16 shows a proposed template for Burst tests.

Project:

Test Item:

Responsible Party:

Tester:

Applied Standard/Guideline:

Type of Device:

Test Location:

Time Range:

2922

Test Requirements/Results:

Type of burst Requirement fulfilled? Achieved Immunity Test Voltage kV Performance Criterion

yes no

power supply lines

data lines

2923

Result:

Test requirements are fulfilled

not fulfilled

Every single requirement must be met.

2924

2925 Remarks:

2926 ______________________________________________________________________________________________________

2927 ______________________________________________________________________________________________________

2928 ______________________________________________________________________________________________________

2929 ______________________________________________________________________________________________________

2930 ______________________________________________________________________________________________________

2931

2932 Enclosures:

2933 ______________________________________________________________________________________________ 2934 ______________________________________________________________________________________________________

2935

2936

2937

2938 Date Tester’s Signature

### 2939 Figure 16 – Proposed template for Burst tests

Version V1.1.5 – 404 – Test Specification © IO-Link

2940

2941

### Test Specification © IO-Link – 405 – Version V1.1.5

### 2942 10.4.5 Conducted RF

### 2943 Figure 17 shows a proposed template for conducted RF tests.

Project:

Test Item:

Responsible Party:

Tester:

Applied Standard/Guideline:

Type of Device:

Test Location:

Time Range:

2944

Test Requirements/Results:

Type of Frequency MHz Requirement fulfilled? Achieved Immunity Test Voltage V Performance Criterion

yes no

0,15 to 80

2945

Result:

Test requirements are fulfilled

not fulfilled

Every single requirement must be met.

2946

2947 Remarks:

2948 ______________________________________________________________________________________________________

2949 ______________________________________________________________________________________________________

2950 ______________________________________________________________________________________________________

2951 ______________________________________________________________________________________________________

2952 ______________________________________________________________________________________________________

2953

2954 Enclosures:

2955 ______________________________________________________________________________________________ 2956 ______________________________________________________________________________________________________

2957

2958

2959

2960 Date Tester’s Signature

### 2961 Figure 17 – Proposed template for conducted RF tests

2962

Version V1.1.5 – 406 – Test Specification © IO-Link

2963 10.4.6 Explanation of template terms

2964 The terms in the templates are defined as follows:

2965 • "Project" means for example the name of an SDCI Device.

2966 • "Test Item" means the name and order number of the particular Device under test.

2967 • "Responsible Party" means the manufacturer or a third-party company who takes responsi- 2968 bility for the Device.

2969 • "Tester" means the full name of the test person in charge.

2970 • "Applied standards or guidelines" shall comprise at least [1] and a product standard such 2971 as IEC 60947-1

2972 • "Type of Device" identifies the type of the device thus indicating the appropriate level of 2973 EMC test. Possible types are "open type", "cabinet" or "enclosed type".

2974 • "Test Location" indicates the name and address of the EMC test laboratory.

2975 • "Time Range" indicates the date and the duration of the test.

2976

### Test Specification © IO-Link – 407 – Version V1.1.5

2977

## 2978 (normative)

## 2979 Test configurations and test tools

### 2980 A.1 Test configurations

### 2981 A.1.1 Overview

### 2982 The test cases for the physical layer tests and data link layer tests can be performed with the

### 2983 help of

### 2984 • A variable power supply between 20 V and 30 V

### 2985 • Discrete components such as capacitors and resistors according to the test case

### 2986 • A voltmeter (accuracy of 2 %) and a current meter (accuracy of 10 %)

### 2987 • An oscilloscope for Wake-up pulses and eye-diagrams

### 2988 • A logic analyzer for message timings

### 2989 • A line-monitor to record protocol sequences

### 2990 • A Reference-Master and a Reference-Device

2991

### 2992 A.1.2 Measurement circuits for electrical isolation

### 2993 A.1.2.1 Measurement of Master Port class B isolation

### 2994 Figure A.1 shows the measurement circuit for electrical isolation of Master Port class B.

2995

### 2996 Figure A.1 – Measurement of Master Port class B isolation

### 2997 A.1.2.2 Measurement of Device isolation

### 2998 Figure A.2 shows the measurement circuit for electrical isolation of Device power supplies.

2999

### 3000 Figure A.2 – Measurement of Device isolation

3001

1

2

4

3

5

Port Class B (M12)

Power 1 power supply

Power 2 extra power supply

P24 (Act)

N24 (Act)

C/Q

Power 2 not connected Current meter

Vaux

IgiM0/1

10 kOhm

PSM

1

2

4

3

5

Device

C/Q

P24 (Act)

N24 (Act)

Current meter

Vaux

IgiD0/1

VSD

10 kOhm

Version V1.1.5 – 408 – Test Specification © IO-Link

3002 A.1.3 Measurement circuits for the physical layer tests

3003 A.1.3.1 Measurement of static parameters

3004 Figure A.3 shows the measurement circuit diagram for static parameters with the help of a 3005 voltmeter.

3006

3007 Figure A.3 – Measurement circuit diagram for static parameters

3008

3009 A.1.3.2 Measurement of power supply behavior

3010 Figure A.4 shows the circuit diagram for the measurement of the power-on behavior of Devices.

3011

3012 Figure A.4 – Measurement circuit diagram for power supply behavior

3013

3014 A.1.3.3 Measurement of dynamic parameters

3015 Figure A.5 shows the measurement circuit diagram for dynamic parameters with the help of an 3016 oscilloscope.

3017

3018 Figure A.5 – Measurement circuit diagram for dynamic parameters

C/Q

L+

L- Oscilloscope

24 V

S 1

Variable supply voltage

Device (EUT)

Test Specification © IO-Link – 409 – Version V1.1.5

3019

3020 A.1.3.4 Measurement of Master input thresholds

3021 Figure A.6 shows the measurement circuit diagram for Master input thresholds with the help of 3022 an auxiliary variable voltage and a voltmeter.

3023

3024 Figure A.6 – Measurement circuit diagram for input thresholds

3025

3026 A.1.3.5 Measurement of Wake-up requests (high)

3027 Figure A.7 shows the measurement circuit diagram for Wake-up requests with the help of an 3028 oscilloscope if the steady state level (of a Device) is high.

3029

3030 Figure A.7 – Measurement circuit diagram for Wake-up requests (high)

3031

3032 A.1.3.6 Measurement of Wake-up requests (low)

3033 Figure A.8 shows the measurement circuit diagram for Wake-up requests with the help of an 3034 oscilloscope if the steady state level (of a Device) is low.

IO-Link

Master (EUT)

variable PSM (PSMmin, PSMmax e.g. 90 to 240 V AC, or 24 to 30 V DC) C/Q

L+

L- 2

1

+ −

Voltmeter Variable voltage VIM

### Version V1.1.5 – 410 – Test Specification © IO-Link

3035

### 3036 Figure A.8 – Measurement circuit diagram for Wake-up requests (low)

3037

### 3038 A.1.3.7 Measurement of return time delay to SIO mode

### 3039 Figure A.9 shows the circuit diagram for measurements of the delay time of a Device to return

### 3040 to SIO-mode with the help of an oscilloscope.

3041

### 3042 Figure A.9 – Measurement of return time delay to SIO mode

3043

### 3044 A.1.3.8 Measurement of dynamic parameters (Device input threshold)

### 3045 Figure A.10 shows the measurement circuit diagram for Device input thresholds with the help

### 3046 of an auxiliary variable voltage and a voltmeter.

3047

### 3048 Figure A.10 – Measurement circuit diagram for input thresholds

R2

R2 = 680 Ohm or current sink = 5 mA

24 V C/Q

L+

L- Device (EUT)

S 1

Oscilloscope

C/Q

L+

L- Device (EUT)

24 V Variable power supply (min/max) VSD

2

1

+ −

Voltmeter Variable voltage VID

### Test Specification © IO-Link – 411 – Version V1.1.5

3049

### 3050 A.1.3.9 Measurement of Wake-up requests (timing)

### 3051 Figure A.11 shows the measurement circuit diagram for the timing of Wake-up requests with

### 3052 the help of an oscilloscope.

3053

### 3054 Figure A.11 – Measurement circuit diagram for Wake-up request timings

3055

### 3056 A.1.3.10 Measurement of Receive Enable after Wake-up (C/Q high)

### Figure A.12 shows the circuit diagram for the measurement timing of t

### REN (receive enable de-

3057

### 3058 lay) with the help of an oscilloscope in case of C/Q = high.

3059

### Figure A.12 – Measurement circuit diagram for timing t

### REN (C/Q high)

3060

3061

### 3062 A.1.3.11 Measurement of Receive Enable after Wake-up (C/Q low)

### Figure A.13 shows the circuit diagram for the timing measurement of t

### REN (receive enable de-

3063

### 3064 lay) with the help of an oscilloscope in case of C/Q =low.

C/Q

L+

L-

Oscilloscope or logic analyzer

# ~

Signal generator

24 V

Variable power supply (min/max)

WURQ indication

Device (EUT)

R1

R2

tREN

C/Q = high

WURQ

V0

VTHLmin

R1 = 1,2 kOhm R2 = 390 Ohm

C/Q

L-

Oscilloscope

# ~

Signal generator

24 V

Device (EUT)

### Version V1.1.5 – 412 – Test Specification © IO-Link

3065

### Figure A.13 – Measurement circuit diagram for timing t

### REN (C/Q low)

3066

3067

### 3068 A.1.3.12 Measurement of start-up and readiness timings

### 3069 Figure A.14 shows the measurement circuit diagram for start-up and readiness timings with the

### 3070 help of an oscilloscope or a logic analyzer.

3071

### 3072 Figure A.14 – Measurement of start-up and readiness timings

3073

### 3074 A.1.3.13 Eye diagram measurement using a line simulation

### 3075 Figure A.15 shows the circuit diagram for the eye diagram measurements using a line simulation

### 3076 as required load.

R1

R2

R1 = 680 Ohm R2 = 1,2 kOhm

Oscilloscope

tREN

C/Q = low

WURQ

VTHHmax

V0

# ~

Signal generator

24 V C/Q

L+

L- Device (EUT)

Oscilloscope or logic analyzer

IO-Link

Reference-Master

C/Q

L+

L-

C/Q

L+

L- Device (EUT)

### Test Specification © IO-Link – 413 – Version V1.1.5

3077

### 3078 Figure A.15 – Eye diagram measurement using a line simulation

3079

### 3080 A.1.4 Protocol recording via a Line-Monitor

### 3081 Usually, the test cases assume that a test passed if data are written or read in the expected

### 3082 manner. Sometimes it is easier to observe the protocol steps with the help of a Line-Monitor

### 3083 that lists the Master request messages and the Device response messages in a convenient

### 3084 manner on the screen of a laptop. Figure A.16 shows the principle.

3085

### 3086 Figure A.16 – Message recording via Line-Monitor

3087

### 3088 A.1.5 Requirements for a Reference-Master and a Reference-Device

### 3089 The features in Table A.1 are mandatory for a Reference-Master as referred to in the physical

### 3090 layer tests of Devices.

### 3091 Table A.1 – Mandatory features of a Reference-Master

Number Feature Reference

FRM1 Full compliance with respect to the signal on C/Q [1]

FRM2 VSM voltage adjustable in the range from 20V to 30V Master with nominal 24 V

FRM3 CQM selectable between 500 pF and 1 nF [1], Table 6

FRM4 Functionality of the following SMI services: [1], 11.2 and Annex E

R = 1 Ohm C1 = 820 pF C2 = 680 pF

Oscilloscope

R R R

R R R

R R R

C2

C2 C1

C1

Oscilloscope

IO-Link

Master (MTU)

C/Q

L+

L-

C/Q

L+

L- Device (EUT)

Line monitor

IO-Link

Master (EUT)

C/Q

L+

L-

C/Q

L+

L- Device (EUT)

### Version V1.1.5 – 414 – Test Specification © IO-Link

a) SMI_PortPowerOffOn(ABPS_PORTPOWERON), b) SMI_PortConfiguration(ABPS_NO_TYPE_CHECK), c) SMI_PortConfiguratoin(ABPS_PORT_INACTIVE) d) SMI_PortConfiguration(ABPS_TYPE_COMP)

For parameter sets see Table A.10

3092

### 3093 The features in Table A.2 are mandatory for a Reference-Device as referred to in the physical

### 3094 layer tests of Masters.

### 3095 Table A.2 – Mandatory features of a Reference-Device

Number Feature Reference

FRD1 Full compliance with respect to the signal on C/Q [1]

FRD2 Transmission rates selectable between COM2 and COM3 [1], Table 9

FRD3 CQD selectable between 500 pF, 1 nF, and 10 nF [1], Table 7

3096

### 3097 A.1.6 Diagrams for evaluations

### 3098 Figure A.17 demonstrates how to determine maximum and minimum durations of Wake-up

### 3099 pulse high.

3100

### 3101 Figure A.17 – Evaluation of Wake-up pulse duration high

### 3102 Figure A.18 demonstrates how to determine maximum and minimum durations of Wake-up

### 3103 pulse low.

3104

VID,M

V0

V+

VTHHMIN

VTHHMAX

Voltage range 'H'

Voltage range 'L'

Threshold 'L' Threshold 'H' VTHLMAX

VTHLMIN

t1 t2

t3 t4

tWUmax

tWUmin

VID,M

V0

V+

VTHHMIN

VTHHMAX

Voltage range 'H'

Voltage range 'L'

Threshold 'L' Threshold 'H' VTHLMAX

VTHLMIN

t1 t2

t3 t4 tWUmax

tWUmin

Test Specification © IO-Link – 415 – Version V1.1.5

3105 Figure A.18 – Evaluation of Wake-up pulse duration low

3106

3107 A.2 Device-Tester-System

3108 A.2.1 Overview

3109 To facilitate the tests of Devices and to ensure highest levels of conformity, several tools and 3110 the associated requirements (see Table A.3) are defined. These tools shall be type-approved 3111 by the organization mentioned in D prior to any conformity testing for a manufacturer declara- 3112 tion.

3113 A.2.2 Test principle and requirements

3114 Figure A.19 shows the principle of a Device-Tester-System comprising

3115 • A Device-Tester-Unit hardware with at least one SDCI port, which can be a modified stand- 3116 ard Master with an adequate communication interface to a personal computer,

3117 • A personal computer supporting the communication interface of the Device-Tester-Unit 3118 hardware,

3119 • A Device-Tester-Program running on that personal computer serving as a control and mon- 3120 itorring program for the Device-Tester-Unit hardware,

3121 • A Device – the EUT – that shall be tested for conformity.

3122

3123 Table A.3 – Requirements for the Device-Tester-System

Requirement Description

SR1 The Device-Tester system shall execute and evaluate the test cases defined in this specification. This can include some functions or behavior not defined in the SDCI specification but is necessary to run the EUT into a specific state, e.g. generation of checksum errors. SR2 The result of each test case and additional information about the test execution shall be reported to the user (test report, log file). The user shall be able to store and print this information. SR3 The conformity test cases shall be secured against manipulation. SR4 Optional requirement: The Device-Tester can interpret a valid IODD and generate different settings which are required for the conformity test. In case of absence of the IODD file there shall be a possibility to edit the settings manually.

3124

Version V1.1.5 – 416 – Test Specification © IO-Link

3125

3126 Figure A.19 – Principle of a Device-Tester-System

3127

3128 A.3 Master-Tester-System and approach to Master testing

3129 A.3.1 Master-Tester using common communication interfaces

3130 Figure A.20 shows the principle of a Master-Tester-System comprising

3131 • A Master-Tester-Unit (MTU), which is a configurable and observable Device emulator with 3132 an IO-Link Port and with any communication interface to a personal computer, e.g. USB 3133 (Universal Serial Bus),

3134 • A personal computer supporting the communication interface of the MTU and a communi- 3135 cation interface to the SMI services (SMI Test Communication Server) of the Master to be 3136 tested (EUT = Equipment Under Test),

3137 • A Master-Tester-Program running on that personal computer serving as a control and mon- 3138 itoring program for the MTU as well as for the EUT,

3139 • A Master – the EUT – that shall be tested for conformity and that provides at least one Port 3140 and usually a communication interface based on Ethernet to an upper level system or in 3141 this case to the personal computer.

3142 To perform a test case, the Master-Tester-Program uses SMI service instances with appropriate 3143 ArgBlock parameters (ABPS) to establish preconditions for the EUT (Master) and to step 3144 through the procedure.

3145 In addition, the Master-Tester-Program also communicates with the Master-Tester-Unit using 3146 "Control/Status" instructions via a common interface, e.g. USB. These MTU-Instructions are 3147 identified by a characteristic and intuitive name.

3148

Device- Tester Unit

e.g. USB

SDCI

Device-Tester-Program

Tester driver

Device (EUT)

Test script based on Test Cases

IODD of SDCI Device Log file Test report file

### Test Specification © IO-Link – 417 – Version V1.1.5

3149

### 3150 Figure A.20 – Principle of a Master-Tester-System using SMI

### 3151 A.3.2 System requirements for Master-Tester

### 3152 The tools of a Master-Test-System shall be type-approved by the organization mentioned in D

### 3153 prior to any conformity testing for a manufacturer declaration.

### 3154 Table A.4 lists the system requirements for the approval of a Master-Tester-System.

### 3155 Table A.4 – System requirements for the Master-Tester

Requirement Description

SR1 The test system shall execute and evaluate the test cases defined in this specification

SR2 It should be possible to define, execute and evaluate additional customer specific test cases.

SR3 The result of every test case and also additional information about test execution shall be reported to the user (test report, log file). The user shall be able to store and print this information.

SR4 The conformity test cases defined in this specification and the user defined test cases should be coded in a script file (XML or TCL).

SR5 The conformity test script file shall be provided by test system supplier.

SR6 The conformity test script file shall be secured against manipulation.

SR7 For the sake of an approval of the test system, it shall have a TCP/IPcommunication path between EUT (SDCI Master) and the personal computer.

SR10 The test system manual shall be provided as PDF document. The user shall be able to read this document via freely available Adobe Reader software.

SR11 The Master-Tester software can be used to download new firmware updates to the Master- Tester. The download process can not be interrupted by the user. The software can not verify the content of the downloaded file. The user is responsible to use a valid and correct Master- Tester firmware update file.

SR13 The timeout for the time between entering the PREOPERATE state and leaving this state shall be adjustable in the Master tester

3156

### 3157 Table A.5 lists the functional requirements for the approval of a Master-Tester-System.

SMI Test Communication Server

Master (EUT) Master- Tester- Unit (MTU)

e.g. USB

SDCI

Master-Tester-Program including Procedure/Macros (Sequences of SMI_Services + MTU_xyz)

Test interface driver

Tester driver

TCP/IP

SMI services

SMI services

Master-Tester- System

MTU_"Control/Status" instructions SMI_Service instancies (ABPS)

Event

### Version V1.1.5 – 418 – Test Specification © IO-Link

### 3158 Table A.5 – Functional requirements for the performance of test cases

Requirement Description

FR1 Usecase 1: Simulation of an SDCI Device

FR2 Usecase 2: Error behavior (stack-Errors like checksum errors, invalid timing and application errors such as creation of ErrorCodes)

FR3 Usecase 3: Creation of status information (number of transmitted messages by the master, number erroneous messages)

FR4 Usecase 4: Stand-alone device for EMC tests

FR5 Hardware EMC Requirements:

- IEC61000-4-4 (Burst) +/- 2 kV Crit. A; +/- 4 kV Crit. B

- IEC61000-4-6 (RF) 13 V Crit. A

FR6 Hardware Requirements:

- SDCI interface

- Slew Rate > 200 ns

- Signaling LED or display: error counter (with active reset)

- Power LED

- SDCI communication LED

- USB interface V2.0 (API)

- Power supply via SDCI (optional USB or battery or external supply)

- Non-volatile storage of configuration (maximum 1024 octets)

- Monitoring (optional as independent tool)

- Trigger output (24 V/10 mA)

FR7 Configuration areas:

- Device configuration (MinCycleTime, M-sequence Capability, RevisionID, ProcessDataIn,

ProcessDataOut, VendorID, DeviceID, FunctionID, transmission rate)

- IO data configuration (Input data adjustable, mirror output data onto input data, increment

input data)

- Event configuration (maximum 6 events, unique, cyclic, depending on output)

- ISDU configuration (all Indices)

- Stack configuration

FR8 SDCI functionality:

- all transmission rates (4,8; 38,4; 230,4 kbit/s)

- SIO-Mode

- All valid M-sequence types (TYPE_0, TYPE_1_1, TYPE_1_2, TYPE_1_V, TYPE_2_1,

TYPE_2_2, TYPE_2_3, TYPE_2_4, TYPE_2_5, TYPE_2_6, TYPE_2_V)

- All specified IO configurations

- All specified ISDU Indices

- Direct Parameter page (Index 0 and 1)

FR9 Trigger incidences:

- Begin of start-up sequence (Wake-up is detected)

- New Process Data cycle started

- New SDCI M-sequence detected

- Start of a new ISDU request detected

- New ISDU response is generated

- An Event is generated

- Errors (checksum, parity, frame, protocol)

3159

### 3160 A.3.3 SMI Test Communication Server (STCS)

### 3161 In order to perform an automated type testing, it is recommended for a Master manufacturer to

### 3162 provide an SMI Test Communication Server allowing the Master-Tester-Program to send and

### 3163 receive SMI service messages that carry the corresponding ArgBlocks. Send and receive is

### 3164 performed using the Transmission Control Protocol (TCP/IP) on Ethernet communication to the

### 3165 TCP/IP client of the Master-Tester-Program (see Figure A.20).

### 3166 TCP/IP eliminates possible transmission errors and ensures that the transferred packages re-

### 3167 main in the correct order. The Master-Tester-Program sends and receives the SMI service mes-

### 3168 sages as described in [1] in serialized form and with Big-Endian encoding for multiple octet

### 3169 elements. The SMI service messages are self-contained as the receiving side can extract all

### 3170 the information necessary for decoding (length, Port, ClientID, etc.) from the octet stream. Thus,

### 3171 there is no need to add any overhead.

Test Specification © IO-Link – 419 – Version V1.1.5

3172 The STCS consists of a socket listener and a mapping logic converting SMI service messages 3173 into a form that can be understood by the Master (EUT). To establish a TCP/IP connection 3174 between Master-Tester-Program and STCS, the following sequence of steps is performed:

3175 a) STCS listens on a local port (preferred: 49850) to connection requests issued by the Master- 3176 Tester-Program,

3177 b) The Master-Tester-Program requests a connection from the STCS, which it shall accept,

3178 c) A port is created by the Master-Tester-Program and is connected to the corresponding 3179 STCS port,

3180 d) A socket is created on both ends of the connection, and the details of the connection are 3181 encapsulated by the socket,

3182 e) The Master-Tester-Program sends SMI request messages and receives SMI response mes- 3183 sages or event messages,

3184 f) The STCS continues to listen for further connection requests.

3185

3186 The port number 49850 shall be used as default port number. If this port is not available or 3187 already in use, the next available port shall be used. Means for port number reassignment shall 3188 be provided both on the STCS and the Master-Tester-Program client. The following rules apply 3189 to the described TCP/IP connection:

3190 • One STCS shall only correspond with one Master (EUT) instance,

3191 • The STCS shall not store any status information including event queues. The Master-Tester- 3192 Program shall be responsible for queuing and processing events,

3193 • The selected TCP/IP port number shall be within the dynamic, private port range (49152 to 3194 65535). This range is used for private or customized services, for temporary purposes, and 3195 for automatic allocation of ephemeral ports.

3196

3197 The following hints should be considered for the implementation of the STCS and the Master- 3198 Tester-Program client:

3199 • It is not guaranteed over TCP/IP that the sent SMI service messages are transmitted within 3200 one TCP/IP package. Therefore, means for splitting and merging SMI service messages 3201 shall be part of the implementation of the STCS and the Master-Tester-Program.

3202 • The communication interface is full duplex.

3203 • This concept allows for parallel testing of several Master Ports using different STCS con- 3204 nection ports (> 49850).

3205

3206 A.4 Components of Master test cases

3207 A.4.1 Overview

3208 SMI service instances and MTU-Instructions can be directly used to define test cases. In certain 3209 cases, it is advantageous to use macros combining basic SMI service instances and/or MTU- 3210 Instructions. Flow control expressions such as "wait until", "repeat from", etc. can supplement 3211 these macros. Macros shall be named intuitively and shall be preceded by the prefix "TM_" for 3212 test macro. Figure A.21 shows the components of Master test cases.

### Version V1.1.5 – 420 – Test Specification © IO-Link

3213

### 3214 Figure A.21 – Components of Master test cases

### 3215 A.4.2 EUT preconditions (Master Port)

### 3216 Table A.6 shows preconditions of the EUT. They can represent a description of a state or a

### 3217 sequence of activities to reach a certain state of the EUT.

### 3218 Table A.6 – Preconditions of the EUT

Identifier Description of state or activities to reach state

PORT_DI Port is in DI Mode;SMI_PortConfiguration

PORT_DO Port is in DO Mode

PORT_INACTIVE Port is in Inactive Mode

PORT_AUTOSTART Port is in Autostart Mode

NOTE EUT that supports PortPowerOffOn (SMI_Masteridentification; Features_1, Bit 2 = 1), the port power shall be switched on (SMI_PortPowerOffOn; PortPowermode = 2) before the SMI_Portconfiguration command is sent.

3219

### 3220 A.4.3 MTU preconditions

### 3221 Table A.7 shows macros of preconditions of the MTU playing the role of a controllable and

### 3222 observable Device. They describe values (instances) of parameters of a state or a sequence of

### 3223 activities to reach a certain state of the MTU.

### 3224 Table A.7 – Preconditions of the MTU

Identifier Description of state or activities to reach state

MTU_STANDARD_STATE Transmission rate = 38,4 kbit/s;COM2 DS_UPLOAD_FLAG = "0";no DS_UPLOAD_REQ DPP1(MinCycleTime) = 0x28;4ms DPP1(M-sequenceCapability) = 0x11;PREOPERATE = TYPE1_2, ;OPERATE = TYPE_2_5 ;ISDU supported DPP1(RevisionID) = 0x11 (NOTE);Revision = V1.1.x DPP1(ProcessDataIn) = 0x08;PDIn = 8 bit DPP1(ProcessDataOut) = 0x08;PDOut = 8 bit DPP1(VendorID) = 0xFDE8;ID of IO-Link Community (Tester) DPP1(DeviceID) = 002BD2 (NOTE);DID = 11218 DPP1(FunctionID) = 0x0000;FID is reserved Device is activated;Device responds to Master Event flag = 0;no Events pending PD status flag = 1;Process Data valid

Test interface Master (EUT) Master- Tester- Unit (MTU) IO-Link communications

ABPS (Parameter/Sets) SMI_Service instances (ABPS)

SMI services

SMI services

Event

MTU_"Control/Status" instructions

Procedure: Single steps or Test Macros (SMI + MTU)

EUT-Precondition: Configuration (sequence)

MTU instructions

MTU-Precondition: Configuration (sequence) Table A.7

Table A.6

Table A.10

Table A.8

Table A.11

SMTU → Safety- Master-Tester-Unit

### Test Specification © IO-Link – 421 – Version V1.1.5

Identifier Description of state or activities to reach state

Mandatory Indices: Index 0x0010 (VendorName) = "IO-Link Community";UTF8 coding Index 0x0012 (ProductName) = "MTU";UTF8 coding Index 0x0003 (DataStorageIndex) = PARSET1;see Table A.13 All states and counters reset

NOTE Only these IDs can be overwritten by Master for compatibility tests (see 9.5)

Permitted values for RevisionID are: 0x11 and 0x10.

3225

### 3226 A.4.4 TestMacros (TM) of the Master-Tester-Program

### 3227 Table A.8 shows TestMacros of the Master-Tester-Program for both EUT (Master) and MTU.

### 3228 All TestMacros shall return after ≤ 30 s (default Test_Timeout).

### 3229 Table A.8 – TestMacros of the Master-Tester-Program

TestMacro identifier Variable Test Service Action to enter mode Comment

TM_AWAIT Time Pause "Time" (e.g. 2000) before next step Milliseconds

TM_AWAIT_PORT_STATUS NO_DEVICE, PORT_DIAG, OPERATE, PREOPERATE

a) Repeat SMI_PortStatus service until PortStatusList.PortStatusInfo = NO_DEVICE, or PORT_DIAG, or OPER- ATE

Monitored by Test_Timeout

TM_AWAIT_DI_HIGH – Repeat SMI_PDIn until PDIn.PDI0 = 1 and PDIn.PQI = 0x00 Monitored by Test _Timeout

TM_AWAIT_PD_VALIDITY VALID/ INVA- LID Repeat SMI_PDIn until PDIn.PQI.PQ = "1" (VALID) or "0" (INVALID) –

TM_MASTER_UPLOAD PARSET1, PARSET2 (see Table A.13)

a) MTU_DS_SetParameter(<variable>) b) SMI_PortConfiguration(ABPS_TYPE_ COMP, <Validation&Backup = 3>) c) TM_AWAIT_PORT_STATUS(OPERATE) d) SMI_DSToParServ e) MTU_DS_CheckUpload;delete monitoring states

Provides EUT and MTU with DS data object

3230

### 3231 A.4.5 SMI Event Handling

### 3232 Since the SMI services SMI_DeviceEvent and SMI_PortEvent are initiated by the EUT, the "Test

### 3233 interface driver" implementation shall provide a synchronous API to the Master-Tester-Program

### 3234 (see Figure A.18). Events sent by the EUT shall be enqueued into a separate internal queue

### 3235 data structure (one queue for Device Events, one queue for Port Events). The Master-Tester-

### 3236 Program can access these queues by means of the functions in Table A.9.

### 3237 Table A.9 – Event functions for Event tester handling

Event function name Parameter Return value Description

DLL_ClearAllEvents – – Clear both queues

DLL_GetPortEvents minEP List of PortEvents (Qualifier, Code) Buffer up to minEP Port Events in queue or until timeout before returning the list. Ignores optional 0xFF26, 0xFF27, or vendor-specific Events.

DLL_GetDeviceEvents minED List of DeviceEvents (Qualifier, Code) Buffer up to minED Device Events in queue or until timeout before returning the list

### 3238 A.4.6 SMI ArgBlock parameter sets (ABPS)

### 3239 Table A.10 contains a list of ArgBlock parameter sets (ABPS). They contain predefined values

### 3240 for ArgBlock elements. The data types of the elements are specified in [1]. ABPS shall use the

### 3241 prefix "ABPS_".

### 3242 ABPS names can be used in the description of SMI service instances. Predefined elements of

### 3243 an ABPS can be overwritten via the following syntax:

### Version V1.1.5 – 422 – Test Specification © IO-Link

### 3244 ABPS_xxxx<element1 = value, element2 = value, …>

### 3245 For example, ABPS_PORTTODI<VendorID = 286> uses all element definitions of the ABPS_-

### 3246 PORTTODI but replaces the default value "0" the element "VendorID" by the value "286".

### 3247 Table A.10 – ArgBlock Parameter Sets (ABPS)

ABPS ArgBlock Element Type Value

ABPS_VOIDBLOCK VoidBlock ArgBlockID 0xFFF0

ABPS_PORTTODI PortConfigList ArgBlockID Unsigned16 0x8000

PortMode PortMode PM_DI_CQ

Validation&Backup DSType No_Device_Check

I/Q behavior IQ_Behavior IQ_not_supported

PortCycleTime Unsigned8 0

VendorID Unsigned8 0

DeviceID Unsigned8 0

ABPS_PORTTODO PortConfigList ArgBlockID Unsigned16 0x8000

PortMode PortMode PM_DO_CQ

Validation&Backup DSType No_Device_Check

I/Q behavior IQ_Behavior IQ_not_supported

PortCycleTime Unsigned8 0

VendorID Unsigned8 0

DeviceID Unsigned8 0

ABPS_PORTINACTIVE PortConfigList ArgBlockID Unsigned16 0x8000

PortMode PortMode PM_DEACTIVATED

Validation&Backup DSType No_Device_Check

I/Q behavior IQ_Behavior IQ_not_supported

PortCycleTime Unsigned8 0

VendorID Unsigned8 0

DeviceID Unsigned8 0

ABPS_PORTPOWERON PortPowerOffOn ArgBlockID Unsigned16 0x7003

PortPowerMode PortPowerMode PP_Port_Power_On

PortPowerOffTime Unsigned16 0

ABPS_PORTPOWEROFF PortPowerOffOn ArgBlockID Unsigned16 0x7003

PortPowerMode PortPowerMode PP_Port_Power_Off

PortPowerOffTime Unsigned16 0

ABPS_DO_HIGH PDOut ArgBlockID Unsigned16 0x1002

OE PDOutQualityInfo PQ_PDOUTVALID

DO Q_Value Q_High

ABPS_DO_LOW PDOut ArgBlockID Unsigned16 0x1002

OE PDOutQualityInfo PQ_PDOUTVALID

DO Q_Value Q_Low

ABPS_TYPE_COMP PortCycleTime ArgBlockID Unsigned16 0x8000

PortMode PortMode PM_IOL_MANUAL

Validation&Backup DSType TYPE_compatible_Device_V1.1

I/Q behavior IQ_Behavior IQ_not_supported

### Test Specification © IO-Link – 423 – Version V1.1.5

ABPS ArgBlock Element Type Value

PortCycleTime Unsigned8 0x28

VendorID Unsigned16 0xFDE8

DeviceID Unsigned8 0x002BD2

ABPS_NOTYPE_CHECK PortCycleTime ArgBlockID Unsigned16 0x8000

PortMode PortMode PM_IOL_AUTO- START

Validation&Backup DSType No_Device_Check

I/Q behavior IQ_Behavior IQ_not_supported

PortCycleTime Unsigned8 0x28

VendorID Unsigned16 0xFDE8

DeviceID Unsigned8 0x002BD2

ABPS_DEVICEWRITE On-request _Data ArgBlockID Unsigned16 0x3000

Index Unsigned16 0

SubIndex Unsigned8 0

On-request-Data Octet string 0

ABPS_DEVICEREAD On-request _Data ArgBlockID Unsigned16 0x3001

Index Unsigned16 0

SubIndex Unsigned8 0

ABPS_PDOUT PDOut ArgBlockID Unsigned16 0x1002

OE Unsigned8 1

OutputDataLength Unsigned8 1

PDO0 Unsigned8 0x00

3248

### 3249 A.4.7 MTU instructions

### 3250 Table A.11 shows (fixed) instructions of the Master-Tester-Program for the MTU. Every MTU-

### 3251 instruction returns the specified parameters defined in "Return value".

### 3252 Table A.11 – MTU instructions

Name Parameter Return value Definition

MTU_Startup_Check10 – TRUE/FALSE Monitor whether Master connects to Device via standard start-up: a) Initiate wake-up b) Read DPP1 (Address 0x02 to 0x06) c) Write MasterCycleTime d) Write MasterCommand OPERATE e) Change to the appropriate Msequence type (see TestCase) Reset monitoring states after invocation

MTU_Startup_GetLog – MessageLog Returns a log of all Master messages at STARTUP

MTU_DPP1_Get ParameterName Value Get parameter in full length from DPP1 (Direct Parameter Page 1)

MTU_DPP1_Set ParameterName, Value – Set parameter in DPP1, e.g. MTU_DPP1_Set(VendorID = 0xFDE8) Values for PDIn and PDOut lengths presented in bits

MTU_DS_CheckDownload – TRUE/FALSE Monitoring Device activity a) Read Index 3 b) Master sends DS_DownloadStart

### Version V1.1.5 – 424 – Test Specification © IO-Link

Name Parameter Return value Definition c) Master writes Parameter d) Master sends DS_DownloadEnd Returns TRUE if sequence above was performed at least once since start-up Reset monitoring states after invocation and start-up

MTU_DS_CheckDSCommands – TRUE/FALSE Monitoring Device activity whether DS_Download commands or DS_Upload commands have been performed. Reset monitoring states after invocation and start-up

MTU_DS_CheckParameter PARSET1, or PARSET2 TRUE/FALSE Returns TRUE if requested PARSET is active

MTU_DS_CheckUpload – TRUE/FALSE Checks Device activity a) Read Index 3 b) Master sends DS_UploadStart c) Master reads Parameter d) Master sends DS_UploadEnd Returns TRUE if sequence above was performed at least once since start-up Reset monitoring states after invocation and start-up

MTU_DS_Locked – – Parameter DSIndex.StateProperty = “Data Storage locked”

MTU_DS_SetMaxDataStorage MAXDATA, or MAXINDEXLIST – Activates Index_List MAXINDEXLIST or MAXDATA as shown in Table A.11

MTU_DS_SetParameter PARSET1, or PARSET2 – MTU activates PARSET1 or PARSET2 (see Table A.13)

MTU_DS_SetUpload Flag_off, or Flag_on, or Event

TRUE/FALSE Set upload behavior on MTU side: Sets/resets DS_UPLOAD_FLAG or raises a DS_UPLOAD_REQ Event and sets DS_UPLOAD_FLAG.

MTU_DS_SetError IndexList_err, or R_Index19_err, or W_Index19_err

– MTU to generate ErrorType = 0x8023 "Access denied". Either via

- IndexList_err: Read on Index 3,

Subindex 05

- R_Index19_err: Read on Index 19

- W_Index19_err: Write on Index 19

MTU_DS_Wait DS_BREAK, or DS_DOWN- LOADEND, or DS_UPLOAD END

– MTU delays processing until it detects the chosen parameter. Monitored by Test_Timeout.

MTU_Event_CheckNoDetail – TRUE/FALSE Check if Master reads Event without details and acknowledges within Test_Timeout a) Read Event StatusCode (address "0") in next cycle b) Write Event StatusCode (address "0") within Test_Timeout c) Master does not read other Event memory addresses

MTU_Event_Clear – – Clear all values in Event memory

MTU_Event_SetStatusCode code – Set StatusCode of the Event memory (address "0") to "code"

MTU_Event_SetSlot Slot number, Qualifier, Code – Set Event Qualifier and Code of slot number in the Event memory

MTU_Event_Trigger- AndWaitForAck – StatusCode- Read, AllSlotsRead, Acknowledge

a) Set Event flag in Msequence.CKS b) Wait until Master Write to StatusCode (address "0" in Event memory, acknowledge) or until Test-Timeout. c) Return StatusCodeRead = TRUE if Master read the StatusCode

### Test Specification © IO-Link – 425 – Version V1.1.5

Name Parameter Return value Definition d) Return AllSlotsRead = TRUE if Master read all Qualifier and Code values of the activated Events (indicated by StatusCode, always TRUE for StatusCode type 1) e) Return Acknowledge = TRUE if Master wrote to StatusCode and = FALSE upon Test-Timeout

MTU_Event_ISDUInterrupted – TRUE/FALSE Return TRUE if the master switched to communication channel Diagnosis while last ISDU transfer was running

MTU_Event_SetFlag – – In PREOPERATE, OPERATE: Set Event flag in MSequence.CKS immediately. Else: Set Event flag when PREOPER- ATE is reached.

MTU_ISDU_Add Index, Subindex, Value (Octet- String), ErrorType_W, ErrorType_R, Specialty

– Adds virtual ISDU to parameter-space. Length of OctetString reflects size ([0x01, 0x02…]). Master Read or Write instruction returns ErrorType_R/W as defined in Table C.1. [1] respectively, for example 0x8033. Specialties see Table A.14.

MTU_ISDU_Read Index, Subindex, Value (Octet- String) Returns ISDU Parameter content as OctetString value

MTU_ISDU_TrailingIdles – NumIdles Return minimum number of "IDLE 1" requests between ISDU services. Reset monitoring states after invocation.

MTU_ISDU_Write Index, Subindex, Value (Octet- String)

– Writes Value defined as OctetString to virtual or real ISDU.

MTU_PD_Get – OctetString Return PDout values

MTU_PD_Set OctetString – Set PDin values

MTU_State_Activate – – Enable response to Master request

MTU_State_Deactivate – – Disable response to Master request

MTU_State_BlockFallback n – MTU does not respond to the next n Fallback commands

MTU_State_CheckFallback – TRUE/FALSE Observe whether MasterCommand "Fallback" has been sent

MTU_State_CheckOperate – TRUE/FALSE TRUE if MTU is in OPERATE

MTU_State_CheckPreoperate – NONE/ REACHED/ PASSED

MTU returns information on occurrence of state PREOPERATE. Reset when the communication is restarted (STARTUP state is entered)

MTU_State_GetMaster- RetryCTviolCount – RETRIES, CTVIOLS Provides Retries and CycleTime violations. Values shall be cleared after performance.

MTU_State_CountRestarts – Restarts MTU counts number of MTU restarts. Reset after invocation.

MTU_State_IncorrectChecksums n – n reply messages to Master with incorrect checksums

MTU_State_MirrorPD – – Applies only for PDInLength = PDOutLength. Mirrors PDOut to PDIn within the same M-sequence.

MTU_State_SkipResponse n WURQ – Skip n reply messages to Master (at the expected COM speed). If WURQ = TRUE responses are skipped at wake up sequence, default WURQ = FALSE

### Version V1.1.5 – 426 – Test Specification © IO-Link

Name Parameter Return value Definition

MTU_State_SetPDValidity VALID, or INVALID – Set PD validity to a given value. The MTU decides if it will propagate the information via flag (V1.1) or via Event (V1.0) whenever validity changed

MTU_Timing_Startup – TDWU12 TDWU23 WURQ WURQ101-500 TSD TDMT(COM1) TDMT(COM2) TDMT(COM3)

Returns timing measurement values in ms during start-up

MTU_Timing_GetTinicyc- InStartup – Min Measure cycle times in STARTUP (minimum recovery time)

MTU_Timing_GetTinitcyc- InPreoperate – Min Measure cycle times in PREOPERATE

MTU_Timing_GetTcyc- InOperate – Min Measure cycle times in OPERATE during 20 Master cycles

MTU_Timing_SetReplyMessage Delay TA, or T2 – in TBIT; default values: TA = 5 TBIT, T2 = 0 TBIT

MTU_Timing_SetCommunication Mode COM1, COM2, COM3 – Transmission rates: COM1 = 4,8 kbit/s COM2 = 38,4 kbit/s COM3 = 230,4 kbit/s

MTU_Timing_GetT1 – Max Measure the delays between the end of the stop bit and the beginning of the start bit of the next octet

3253

### 3254 A.4.8 MTU data sets

### 3255 A.4.8.1 MTU Index lists for Data Storage tests

### 3256 Table A.12 shows the two data sets used for Data Storage testing of Masters (see Table B.10

### 3257 and G.1 in [1]).

### 3258 Table A.12 – MTU Index lists for Data Storage tests

Item MAXINDEXLIST (Concatenated) MAXDATA per object

DataStorage Index

Index 3, Subindex 03 (Data_Storage_Size) = maximum size (2048 octets) Index 3, Subindex 03 (Data_Storage_Size) = size (2048 octets)

Index_List Six concatenated Index_Lists. Two of them with 35 entries, four of them with 70 entries, the sixth with 59 entries plus Termination_Marker

[35] → [35] → [70] → [70] → [70] → [70] → [59]─┤

Single Index_List contains 9 entries (X1 to X9) plus Termination_Marker.

[9]─┤

Entries 408 Objects (2 octets Index, 1 octet Subindex, 1 octet length) with 1 octet data length > 2040 octets. 8 objects with 232 octets data length plus 1 object with 156 octets data length. This leads to a total size of 8 x (4+232) + 1 x (4+156) = 2048 octets for the structure defined in Annex G.1 in [1].

3259

### 3260 A.4.8.2 MTU parameter sets

### 3261 Table A.13 shows the parameter sets used for MTU instructions "MTU_DS_SetParameter" and

### 3262 "MTU_DS_CheckParameter" (see Table A.11).

### Test Specification © IO-Link – 427 – Version V1.1.5

### 3263 Table A.13 – MTU parameter sets

Name Content 1 Content 2

PARSET1 Index: 64 Subindex 1: [0x12, 0x34] Subindex 2: [0x35, 0x69]

Index: 256 Subindex1: [0x79, 0x85] Subindex2: [0x92, 0x23]

PARSET2 Index: 64 Subindex 1: [0x47,0x11] Subindex 2: [0x98,0x76]

Index: 256 Subindex 1: [0x97,0x85] Subindex 2: [0x40,0x40]

3264

### 3265 A.4.8.3 MTU specialties

### 3266 Table A.14 shows specialties used for MTU instructions "MTU_ISDU_Add" (see Table A.11).

### 3267 Table A.14 – MTU specialties

Name Definition

NO_DEVICE_BUSY MTU generates an immediate response to the ISDU request without indicating "Device busy" (no I-Service/Length = 0x01 responses, see Table A.14 in [1]).

DEVICE_BUSY MTU responds to the ISDU request indicating "Device busy" (at least one I-Service/Length = 0x01 response, see Table A.14 in [1])

ISSUE_EVENT Event (DL, Error, Event single shot, EventCode = "0x5200")

TIMEOUT Device does not respond (protocol error)

INCORRECT_SERVICE_CODE Device responds with incorrect service code (I-Service/Length = 0x02, see Table A.14 in [1]) (sequence) (protocol error)

INCORRECT_CHKPDU Device creates incorrect CRC signature within response (protocol error)

RESERVED_DATA_LENGTH Device uses reserved combinations of iService and length (I- Service/Length = 0x10, see Table A.14 in [1])

EVENT Set the Event flag bit in M-sequence.CKS on the next M-sequence after flowCTRL = 2 for the ISDU request/response on ISDU Write/Read access

NO_SERVICE MTU responds directly (no busy responses) with “No service” (I-Service/Length = 0x00 response, see Table A.14 in [1])

3268

Version V1.1.5 – 428 – Test Specification © IO-Link

3269

### 3270 (normative)

### 3271 Supplement to the legacy specification V1.0

3272 B.1 General

3273 The definitions in [1] are more comprehensive than the definitions in the predecessor [5]. In 3274 order to establish a reliable interoperation of legacy Master and Devices with their SDCI coun- 3275 terparts it is necessary to supplement the predecessor specification [5] by a few clarifications.

3276 B.2 Legacy-Master power-on driver capability

3277 If the actual power-on driver capability does not meet the requirements defined in [1], the meas- 3278 ured value(s) of TC_0002 (6.2.2) shall be documented in the user manual of the Legacy-Master.

3279 B.3 Legacy-Device power-on current consumption

3280 If the actual power-on current consumption does not meet the requirements defined in [1], the 3281 measured value(s) of TC_0012 (6.3.2) shall be documented in the user manual of the Legacy- 3282 Device.

3283 B.4 ISDU request and response abort

3284 The "abort" feature is not specifically defined in [5]. All Legacy-Devices shall have implemented 3285 this behavior, which is tested in TC_0067 (7.5.17) and TC_0068 (7.5.18).

3286 B.5 "Device 1.1" connected to a "Master 1.0"

3287 A manufacturer or vendor of a Device without backward compatibility (V1.0 not supported) shall 3288 document in product sheet or user manual that the Device supports IO-Link V1.1 only (7.8.2.2).

3289 B.6 Maximum MasterCycleTime

3290 The maximum MasterCycleTime for both Master and Legacy-Master is 134 ms. This limit is 3291 checked in TC_0089 (7.9.1).

3292 B.7 Maximum MinCycleTime

3293 The maximum MinCycleTime for both Device and Legacy-Device is 134 ms. This limit is 3294 checked in TC_0090 (7.9.2).

3295 B.8 Write access to reserved system commands

3296 The following System commands shall not be tested in Legacy-Devices: 0x5A; 0x8D to 0x8F; 3297 0x97 to 0x99.

3298 A Write access to reserved system commands within a Legacy-Device returns a negative re- 3299 sponse: PAR_VALOUTOFRNG (0x8030). TC_0104 (7.10.2) is affected.

3300 B.9 Time-out for Write access to system commands

3301 Legacy-Devices shall respond within 5 s.

3302 B.10 Text string length for Application Specific Tag

3303 Existing Legacy-Devices are permitted to have text string length <16 octets. In this case, the 3304 manufacturer or vendor shall document the text string length in the user manual. It is highly 3305 recommended to provide a minimum of 16 octets. TC_0122 (7.10.19) and TC_0123 (7.10.20) 3306 are affected.

Test Specification © IO-Link – 429 – Version V1.1.5

3307 B.11 Write access with invalid length

3308 A Write access to reserved system commands within a Legacy-Device returns a negative re- 3309 sponse: PAR_VALOUTOFRNG (0x8030). TC_0141 (7.10.31) and TC_0142 (7.10.32) are af- 3310 fected.

3311 B.12 IODD "reset to factory settings" verification

3312 It is highly recommended for Legacy-Devices to show the behavior defined in [1]. Deviations 3313 shall be documented in the user manual. TC_0155 (0) is affected.

3314 B.13 Fallback in PREOPERATE

3315 If the Master does not support the Fallback through a command from the upper-level system 3316 such as a fieldbus, the manufacturer or vendor of the Device or Legacy-Device respectively 3317 shall document the restriction or behavior in the user manual. TC_0213 (9.8.1) and TC_0214 3318 (9.8.2) are affected.

3319

### Version V1.1.5 – 430 – Test Specification © IO-Link

3320

## 3321 (normative)

## 3322 Listing of test cases

### 3323 C.1 Listing of test cases sorted by IDs

### 3324 Table C.1 shows the Test cases and its references.

### 3325 Table C.1 – Test cases sorted by IDs

SDCI TC ID TC Name Reference

TC_0001 TCM_PHYL_INTF_ISM Table 7

TC_0002 TCM_PHYL_INTF_ISIRM Table 8

TC_0003 TCM_PHYL_INTF_ILLM Table 9

TC_0004 TCM_PHYL_INTF_VRESHIGH Table 10

TC_0005 TCM_PHYL_INTF_VRESLOW Table 11

TC_0006 TCM_PHYL_INTF_VTHHM Table 12

TC_0007 TCM_PHYL_INTF_VTHLM Table 13

TC_0008 TCM_PHYL_INTF_VHYSM Table 14

TC_0011 TCD_PHYL_INTF_ISD Table 17

TC_0012 TCD_PHYL_INTF_ISIRD Table 18

TC_0013 TCD_PHYL_INTF_VRESHIGH Table 20

TC_0014 TCD_PHYL_INTF_VRESLOW Table 21

TC_0015 TCD_PHYL_INTF_IQQD Table 22

TC_0016 TCD_PHYL_INTF_VTHHD Table 23

TC_0017 TCD_PHYL_INTF_VTHLD Table 24

TC_0018 TCD_PHYL_INTF_VHYSD Table 25

TC_0021 TCM_PHYL_INTF_IQWUH Table 28

TC_0022 TCM_PHYL_INTF_TWUH Table 29

TC_0023 TCM_PHYL_INTF_IQWUL Table 30

TC_0024 TCM_PHYL_INTF_TWUL Table 31

TC_0025 TCD_PHYL_INTF_TWUH Table 32

TC_0026 TCD_PHYL_INTF_TWUL Table 33

TC_0027 TCD_PHYL_INTF_TRENHIGH Table 34

TC_0028 TCD_PHYL_INTF_TRENLOW Table 35

TC_0029 TCD_PHYL_INTF_TRDL Table 36

TC_0030 TCM_PHYL_INTF_BITEYEMAXLOAD Table 39

TC_0031 TCM_PHYL_INTF_BITEYEMINLOAD Table 41

TC_0032 TCM_PHYL_INTF_UARTEYEMAXLOAD Table 43

TC_0033 TCM_PHYL_INTF_UARTEYEMINLOAD Table 45

TC_0034 TCD_DLPC_STUP_CYCTIME Table 52

TC_0035 TCD_DLPC_STUP_STUPOPER1 Table 53

TC_0036 TCD_DLPC_STUP_STUPOPER2 Table 55

TC_0037 TCD_DLPC_OPER_OPERSTUP1 Table 56

TC_0038 TCD_DLPC_OPER_OPERSTAR2 Table 57

TC_0039 TCD_DLPC_PROP_READDPP1 Table 58

TC_0040 TCD_DLPC_PROP_WRITEDPP1 Table 59

### Test Specification © IO-Link – 431 – Version V1.1.5

TC_0041 TCD_DLPC_PROP_SHORTMESSAGE Table 60

TC_0043 TCD_DLPC_PROP_SIMRESET Table 61

TC_0044 TCD_DLPC_PROP_MSEQFAULT Table 62

TC_0045 TCD_DLPC_OPER_READ Table 64

TC_0046 TCD_DLPC_OPER_WRITE Table 65

TC_0047 TCD_DLPC_OPER_NEGWRITE Table 66

TC_0049 TCD_DLPC_OPER_SIMRESET Table 67

TC_0052 TCD_DLPC_ISDU_AVAILMSEQCAP Table 71

TC_0053 TCD_DLIC_ISDU_IDLEBUSYCHECK Table 72

TC_0054 TCD_DLIC_ISDU_READINDEX8 Table 73

TC_0055 TCD_DLIC_ISDU_READ8EXTLENGTH Table 74

TC_0056 TCD_DLIC_ISDU_WRITE8 Table 75

TC_0057 TCD_DLIC_ISDU_READ8RESERVED Table 76

TC_0058 TCD_DLIC_ISDU_READ8NOSUBINDEX Table 77

TC_0059 TCD_DLIC_ISDU_READ16 Table 78

TC_0060 TCD_DLIC_ISDU_WRITE16 Table 79

TC_0061 TCD_DLIC_ISDU_READ16RESERVED Table 80

TC_0062 TCD_DLIC_ISDU_READ16NOSUBINDEX Table 81

TC_0063 TCD_DLIC_ISDU_WRITE8LENOVERRUN Table 82

TC_0064 TCD_DLIC_ISDU_WRITE8WRONGLEN Table 83

TC_0065 TCD_DLIC_ISDU_WRITE8WRONGCHECKSUM Table 84

TC_0066 TCD_DLIC_ISDU_WRITE8ROINDEX Table 85

TC_0067 TCD_DLIC_ISDU_ABORTREADREQ Table 86

TC_0068 TCD_DLIC_ISDU_ABORTREADRESP Table 87

TC_0069 TCD_DLIC_EVNT_OPERSINGLEEVENT Table 89

TC_0070 TCD_DLIC_EVNT_PROPSINGLEEVENT Table 90

TC_0071 TCD_DLIC_EVNT_OPEREVENTCLEAR Table 91

TC_0072 TCD_DLIC_EVNT_OPERCOMMINTERRUPT Table 92

TC_0073 TCD_DLIC_EVNT_OPERPOWERINTERRUPT Table 93

TC_0074 TCD_DLIC_EVNT_OPERAPPEARDISAPPEAR Table 94

TC_0075 TCD_DLIC_EVNT_OPERMULTEVENT Table 95

TC_0076 TCD_DLIC_EVNT_OPERSHORTEVENT Table 96

TC_0077 TCD_APPS_DSUP_NOFLAG Table 98

TC_0078 TCD_APPS_DSUP_VIADOWNLOADSTORE Table 99

TC_0079 TCD_APPS_DSUP_VIADOWNLOADSTORENOWRITE Table 100

TC_0080 TCD_APPS_DSUP_VIALOCALCHANGE Table 101

TC_0081 TCD_APPS_DSUP_PARABREAKABORT Table 102

TC_0082 TCD_APPS_DSDN_PARAMODIFICATION Table 104

TC_0083 TCD_APPS_DSDN_FACTORYRESET Table 105

TC_0084 TCD_APPS_DSDN_PARABREAKABORT Table 106

TC_0085 TCD_DLIC_COMP_STARTUP Table 110

TC_0086 TCD_DLIC_COMP_TYPE1INTERLEAVE Table 111

TC_0087 TCD_DLIC_COMP_PDINVALIDEVENT Table 112

TC_0089 TCD_DLPC_STDP_MASTERCYCLETIME Table 113

TC_0090 TCD_DLPC_STDP_MINCYCLETIME Table 114

### Version V1.1.5 – 432 – Test Specification © IO-Link

TC_0091 TCD_DLPC_STDP_MSEQCAPABILITY Table 115

TC_0092 TCD_DLPC_STDP_REVISIONID Table 116

TC_0093 TCD_DLPC_STDP_PDIN Table 117

TC_0094 TCD_DLPC_STDP_PDOUT Table 118

TC_0095 TCD_DLPC_STDP_VENDORID Table 119

TC_0096 TCD_DLPC_STDP_DEVICEID Table 120

TC_0097 TCD_DLPC_STDP_FUNCTIONID Table 121

TC_0101 TCD_DLPC_STDP_WRITERESPAR Table 122

TC_0104 TCD_DLIC_DEFP_SYSCMDRES Table 124

TC_0107 TCD_DLIC_DEFP_DSINDEX Table 125

TC_0108 TCD_DLIC_DEFP_DSRECORD Table 126

TC_0109 TCD_DLIC_DEFP_ACCESSLOCKSVAL Table 128

TC_0110 TCD_DLIC_DEFP_ACCESSLOCKSINVAL Table 129

TC_0111 TCD_DLIC_DEFP_PROFILCHARAC Table 130

TC_0112 TCD_DLIC_DEFP_PDINDESC Table 131

TC_0113 TCD_DLIC_DEFP_PDOUTDESC Table 132

TC_0114 TCD_DLIC_DEFP_VENDORNAM Table 133

TC_0115 TCD_DLIC_DEFP_VENDORTEXT Table 134

TC_0116 TCD_DLIC_DEFP_PRODUCTNAM Table 135

TC_0117 TCD_DLIC_DEFP_PRODUCTID Table 136

TC_0118 TCD_DLIC_DEFP_PRODUCTTEXT Table 137

TC_0119 TCD_DLIC_DEFP_SERNUM Table 138

TC_0120 TCD_DLIC_DEFP_HARDREV Table 139

TC_0121 TCD_DLIC_DEFP_FIRMREV Table 140

TC_0122 TCD_DLIC_DEFP_TAGVALID Table 141

TC_0123 TCD_DLIC_DEFP_TAGINVALID Table 142

TC_0124 TCD_DLIC_DEFP_ERRCOUNT Table 143

TC_0128 TCD_DLIC_DEFP_DEVSTAT Table 144

TC_0129 TCD_DLIC_DEFP_DETAILDEVSTAT Table 146 – Detailed Device Status – complete object

TC_0130 TCD_DLIC_DEFP_DETAILDEVSTATINACTIVE

TC_0131 TCD_DLIC_DEFP_DETAILDEVSTATACTIVE Table 147

TC_0132 TCD_DLIC_DEFP_PDIN Table 148

TC_0133 TCD_DLIC_DEFP_PDOUT Table 149

TC_0134 TCD_DLIC_DEFP_OFFTIMEVALID Table 150

TC_0136 TCD_DLIC_DEFP_PROFILEPARREAD Table 151

TC_0137 TCD_DLIC_DEFP_PROFILEPARWRITE Table 152

TC_0140 TCD_DLIC_DEFP_WRITETOREADONLY Table 153

TC_0141 TCD_DLIC_DEFP_WRITETOOSHORT Table 154

TC_0142 TCD_DLIC_DEFP_WRITETOOLONG Table 155

TC_0143 TCD_DSBP_APPL_BPDOWNLOAD Table 156

TC_0144 TCD_DSBP_APPL_BPBREAKCMD Table 157

TC_0145 TCD_DSBP_APPL_BPBREAKRESET Table 158

TC_0146 TCD_DSBP_APPL_BPBREAKILLPARAM Table 159

### Test Specification © IO-Link – 433 – Version V1.1.5

TC_0147 TCD_DSBP_APPL_BPBREAK2DOWNLOADS Table 160

TC_0148 TCD_DSBP_APPL_BPBREAKLOCALLOCK Table 161

TC_0149 TCD_IODD_PARV_IDENT Table 169

TC_0150 TCD_IODD_PARV_COMPROFILE Table 170

TC_0151 TCD_IODD_PARV_READVERIFY Table 171

TC_0152 TCD_IODD_PARV_WRITEVERIFY Table 172

TC_0155 TCD_IODD_PARV_FACTORYSETTINGS Table 177

TC_0156 TCD_IODD_PARV_ACCESSLOCK Table 174

TC_0157 TCD_IODD_PARV_INDEXCONSISTENT Table 173

TC_0158 TCM_ PHYL_TIME_TDMT Table 181

TC_0159 TCM_ PHYL_TIME_TDWU Table 182

TC_0160 TCM_ PHYL_TIME_NUMOFWURQS Table 183

TC_0161 TCM_ PHYL_TIME_TSD Table 184

TC_0162 TCM_ PHYL_TIME_TINITCYC Table 185

TC_0163 TCM_ PHYL_TIME_MASTERCYCLETIME Table 189

TC_0164 TCM_ PHYL_TIME_MASTERCYCLETIMEREAL Table 190

TC_0165 TCM_ PHYL_TIME_DEVRESPTIMES Table 191

TC_0166 TCM_ PHYL_TIME_UARTT2 Table 192

TC_0167 TCM_ PHYL_TIME_UARTT1 Table 193

TC_0168 TCM_ DLPD_CYCC_TYPE21BIT8IN Table 194

TC_0169 TCM_ DLPD_CYCC_TYPE22BIT16IN Table 195

TC_0170 TCM_ DLPD_CYCC_TYPE23BIT8OUT Table 196

TC_0171 TCM_ DLPD_CYCC_TYPE24BIT16OUT Table 197

TC_0172 TCM_ DLPD_CYCC_TYPE25BIT8INBIT8OUT Table 198

TC_0173 TCM_ DLPD_CYCC_TYPE1OCTET32IN Table 200

TC_0176 TCM_ DLPD_CYCC_MIRROREDPD Table 203

TC_0177 TCM_ DLPD_CYCC_PDINVALID Table 204

TC_0178 TCM_ DLPD_CYCC_PDVALID Table 205

TC_0179 TCM_DLOD_CYCC_TYPE2VPDXOD1 Table 206

TC_0180 TCM_DLOD_CYCC_TYPE2VPDXOD2 Table 207

TC_0181 TCM_DLOD_CYCC_TYPE2VPDXOD8 Table 208

TC_0182 TCM_DLOD_CYCC_TYPE2VPDXOD32 Table 209

TC_0183 TCM_DLST_CHCK_COMPARAM Table 211

TC_0184 TCM_DLST_CHCK_VIDDID Table 212

TC_0185 TCM_DLST_CHCK_V10VIDDID Table 213

TC_0186 TCM_DLST_CHCK_NONCONFVIDDID Table 214

TC_0187 TCM_DLST_CHCK_CONFVIDDID Table 215

TC_0188 TCM_DLST_CHCK_OVERDIDOK Table 216

TC_0189 TCM_DLST_CHCK_OVERDIDNOK Table 217

TC_0190 TCM_DLST_CHCK_OVERRIDNOK Table 218

TC_0192 TCM_DLST_CHCK_VIDDIDNONCONFIG Table 219

TC_0193 TCM_DLST_CHCK_VIDDIDCONFIG Table 220

TC_0194 TCM_DLST_CHCK_DIDWRONG Table 221

TC_0202 TCM_DLOD_PREP_TYPE0READOD1 Table 223

TC_0203 TCM_DLOD_PREP_TYPE12READOD2 Table 224

### Version V1.1.5 – 434 – Test Specification © IO-Link

TC_0204 TCM_DLOD_PREP_TYPE1VREADOD8 Table 225

TC_0205 TCM_DLOD_PREP_TYPE1VREADOD32 Table 226

TC_0206 TCM_DLOD_PREP_TYPE0WRITEOD1 Table 227

TC_0207 TCM_DLOD_PREP_TYPE12WRITEOD2 Table 228

TC_0208 TCM_DLOD_PREP_TYPE1VWRITEOD8 Table 229

TC_0209 TCM_DLOD_PREP_TYPE1VWRITEOD32 Table 230

TC_0210 TCM_DLOD_OPER_TYPE0READOD1 Table 233

TC_0211 TCM_DLOD_OPER_TYPE0WRITEOD1 Table 234

TC_0212 TCM_DLOD_OPER_TYPE12WRITEOD2 Table 235

TC_0213 TCM_DLFB_PROP_OK Table 236

TC_0214 TCM_DLFB_PROP_FAILS Table 237

TC_0215 TCM_DLFB_OPER_OK Table 238

TC_0216 TCM_DLFB_OPER_FAILS Table 239

TC_0221 TCM_DLCC_RTRY_MAXWURQSUCCESS

TC_0222 TCM_DLCC_RTRY_MAXWURQNOSUCCESS Table 252

TC_0223 TCM_ALIC_AERR_WRITEREJECT Table 253

TC_0224 TCM_ALIC_AERR_WRITEINDEXUNSUPPORTED Table 254

TC_0225 TCM_ALIC_AERR_WRITESUBINDEXNOTSUPPORTED Table 255

TC_0226 TCM_ALIC_AERR_WRITETEMPUNAV Table 256

TC_0227 TCM_ALIC_AERR_WRITEINDEXTEMPANAVLC Table 257

TC_0228 TCM_ALIC_AERR_WRITEINDEXTEMPANAVDC Table 258

TC_0229 TCM_ALIC_AERR_WRITEINDEXRO Table 259

TC_0230 TCM_ALIC_AERR_WRITEINVALIDLEN Table 260

TC_0231 TCM_ALIC_AERR_WRITEPARAMOUTOFRNG Table 261

TC_0232 TCM_ALIC_AERR_WRITEPARAMABOVELIMIT Table 262

TC_0233 TCM_ALIC_AERR_WRITEPARAMBELOWLIMIT Table 263

TC_0234 TCM_ALIC_AERR_WRITEPARAMINVALID Table 264

TC_0235 TCM_ALIC_AERR_WRITEDEVICEAPPFAULT Table 265

TC_0237 TCM_ALIC_AERR_WRITERESERVEDINDEX Table 266

TC_0238 TCM_ALIC_AERR_WRITERESERVEDINDEXNOISDU Table 267

TC_0239 TCM_ALIC_DERR_WRITENOBUSY Table 268

TC_0240 TCM_ALIC_DERR_WRITEAFTERBUSYTIMEOUT Table 269

TC_0241 TCM_ALIC_DERR_ILLSERVICECODE Table 270

TC_0242 TCM_ALIC_DERR_WRONGCHECKSUM Table 271

TC_0243 TCM_ALIC_DERR_READNODATA Table 273

TC_0244 TCM_ALIC_DERR_WRITERESERVEDDL Table 272

TC_0245 TCM_ALIC_LIMT_WRITEMINDATALENGTH Table 274

TC_0246 TCM_ALIC_LIMT_WRITEMAXDATALENGTH Table 275

TC_0248 TCM_ALIC_LIMT_READMAXDATALENGTH Table 276

TC_0249 TCM_ALIC_LIMT_WRITEINDEX8NOSUBINDEX Table 277

TC_0250 TCM_ALIC_LIMT_WRITEINDEX8SUBINDEX8 Table 278

TC_0251 TCM_ALIC_LIMT_WRITEINDEX16SUBINDEX8 Table 279

TC_0252 TCM_ALIC_LIMT_IMMEDIATERESPNOBUSY Table 280

TC_0253 TCM_ALIC_LIMT_IMMEDIATERESPWITHBUSY Table 281

TC_0254 TCM_ALIC_LIMT_WRITEMAXSERVICELEN15 Table 282

### Test Specification © IO-Link – 435 – Version V1.1.5

TC_0255 TCM_ALIC_LIMT_WRITEMINSERVICEEXTLEN17 Table 283

TC_0256 TCM_ALIC_EVNT_NODETAILSNOTIFY Table 284

TC_0261 TCM_ALIC_EVNT_WITHDETAILSSINGLEEVENT Table 285

TC_0262 TCM_ALIC_EVNT_WITHDETAILSDOUBLEEVENT Table 286

TC_0263 TCM_ALIC_EVNT_WITHDETAILSSIXEVENTS Table 288

TC_0264 TCM_ALIC_EVNT_WRITEISDUWITH EVENT Table 289

TC_0265 TCM_ALIC_EVNT_READISDUWITH EVENT Table 290

TC_0266 TCM_ALIC_EVNT_WRITEISDUWITH EVENTDETAILS Table 291

TC_0267 TCM_ALIC_EVNT_READISDUWITH EVENTDETAILS Table 292

TC_0280 TCM_ALIC_STOR_DSLOCKED Table 316

TC_0285 TCM_LGCY_MANY_DETECTANDCONNECT Table 317

TC_0286 TCM_LGCY_MANY_DETECTANDINTERLEAVE Table 318

TC_0287 TCM_LGCY_MANY_EVENTACK Table 319

TC_0288 TCM_LGCY_MANY_IDLEAFTERISDU Table 210

TC_0289 TCM_LGCY_MANY_EVENTINTERRUPTSISDU Table 320

TC_0290 TCM_LGCY_MANY_PDINVALIDEVENT Table 321

TC_0294 TCD_PHYL_INTF_BITEYEMAXLOAD Table 40

TC_0295 TCD_PHYL_INTF_BITEYEMINLOAD Table 42

TC_0296 TCD_PHYL_INTF_UARTEYEMAXLOAD Table 44

TC_0297 TCD_PHYL_INTF_UARTEYEMINLOAD Table 46

TC_0298 TCM_ DLPD_CYCC_TYPE26BIT16INBIT16OUT Table 199

TC_0299 TCM_PHYL_INTF_VOLTRANGECQ Table 15

TC_0300 TCD_PHYL_INTF_VOLTRANGECQ Table 26

TC_0301 TCD_PHYL_INTF_TDELAYTOSIO Table 37

TC_0302 TCD_PHYL_INTF_TTOFALLBACK Table 38

TC_0303 TCM_PHYL_INTF_UARTTRANSDELAY Table 47

TC_0304 TCD_PHYL_INTF_UARTTRANSDELAY Table 48

TC_0305 TCD_PHYL_INTF_RESPONSETIME Table 49

TC_0306 TCD_DLPC_CHCK_OVERRIDOK Table 54

TC_0307 TCM_DLST_CHCK_OVERRIDOK Table 222

TC_0308 TCD_DLIC_EVNTOPER_SINGLENOTIFICATION Table 287

TC_0309 TCD_DLIC_ISDU_ELEMENT_RETRY Table 88

TC_0310 TCM_PHYL_INTF_MRGI Table 16

TC_0311 TCD_PHYL_INTF_DRGI Table 27

TC_0312 TCD_DLPC_OPER_OUTINVALID Table 68

TC_0313 TCD_DLPC_OPER_CONNECTIONLOSS Table 69

TC_0314 TCD_DLPC_DID_OVERWRITE_COMP Table 179

TC_0315 TCD_DLPC_DID_OVERRIDE_INCOMP Table 180

TC_0316 TCD_IODD_PARV_DEVICERESET Table 175

TC_0317 TCD_IODD_PARV_APPLRESET Table 176

TC_0318 TCD_IODD_PARV_BACKTOBOX Table 178

TC_0319 TCD_PHYL_INTF_TRANSMISSIONERRORS Table 50

TC_0320 TCD_PHYL_INTF_DOCISD Table 18

TC_0321 TCD_DSBP_APPL_DSSINGLEPARAM Table 107

TC_0322 TCD_DS_APP_IDLEFLAGCLEAR Table 108

### Version V1.1.5 – 436 – Test Specification © IO-Link

TC_0323 TCD_DLIC_DEFP_DSRECORDMARKER Table 127

TC_0324 TCD_DSBP_APPL_UNEXPECTEDINIDLE Table 162

TC_0325 TCD_DSBP_APPL_WRITEINUPLOAD Table 163

TC_0326 TCD_DSBP_APPL_READINDOWNLOAD Table 164

TC_0327 TCD_DSBP_APPL_UNEXPINUPLOAD Table 165

TC_0328 TCD_DSBP_APPL_SWITCHSTATES Table 166

TC_0329 TCD_DSBP_APPL_UPBREAKRESET Table 167

TC_0330 TCD_DSBP_APPL_DNENDBYUPLOAD Table 168

TC_0331 TCM_ PHYL_TIME_TINITCYC_PREOP_2 Table 186

TC_0332 TCM_ PHYL_TIME_TINITCYC_PREOP_8 Table 187

TC_0333 TCM_ PHYL_TIME_TINITCYC_PREOP_32 Table 188

TC_0334 TCM_ DLPD_CYCC_ TYPE2VOD1IN0OUT32OCTET Table 201

TC_0335 TCM_ DLPD_CYCC_ TYPE2VOD1IN32OUT0OCTET Table 202

TC_0336 TCM_ DLPD_CYCC_TYPE1V_OD8BIT0INBIT0OUT Table 231

TC_0337 TCM_ DLPD_CYCC_TYPE1V_OD32BIT0INBIT0OUT Table 232

TC_0338 TCM_DLCC_RTRY_CKSINCORR2STRTUP Table 240

TC_0339 TCM_DLCC_RTRY_ CKSINCORR2PREOP Table 241

TC_0340 TCM_DLCC_RTRY_ CKSINCORR2OPERATE Table 242

TC_0341 TCM_DLCC_RTRY_CKSINCORR3STRUP&RESTOP Table 243

TC_0342 TCM_DLCC_RTRY_CKSINCORR3PREOP&RESTOP Table 244

TC_0343 TCM_DLCC_RTRY_CKSINCORR3OPER&RESTOP Table 245

TC_0344 TCM_DLCC_RTRY_MISSREP2STRTUP Table 246

TC_0345 TCM_DLCC_RTRY_MISSREP2PREOP Table 247

TC_0346 TCM_DLCC_RTRY_MISSREP2OPER Table 248

TC_0347 TCM_DLCC_RTRY_REPMISS3STRTUP&RESTOP Table 249

TC_0348 TCM_DLCC_RTRY_REPMISS3PREOP&RESTOP Table 250

TC_0349 TCM_DLCC_RTRY_REPMISS3OPER&RESTOP Table 251

TC_0350 TCM_ALIC_EVNT_NODEV Table 293

TC_0351 TCM_ALIC_EVNT_PREOPERATE Table 294

TC_0352 TCM_ALIC_EVNT_PORT_DIAG Table 295

TC_0353 TCM_ALIC_EVNT_INVALID_CYCLE_TIME Table 296

TC_0354 TCM_ALIC_STOR_DELETEDSAFTERRECONF Table 297

TC_0355 TCM_ALIC_STOR_MAXSIZELIMITS Table 298

TC_0356 TCM_ALIC_STOR_DSFROMPARAMSERV Table 299

TC_0357 TCM_ALIC_STOR_INCONSDSFROMPARSERV Table 300

TC_0358 TCM_ALIC_STOR_DSTOIMPROPPORTCONF Table 301

TC_0359 TCM_ALIC_STOR_UPLOADREQEVENTOPERBACKREST Table 302

TC_0360 TCM_ALIC_STOR_UPLOADREQEVENTOPERREST Table 303

TC_0361 TCM_ALIC_STOR_UPLOADREQEVENTOPERBACKRESTINACT Table 304

TC_0362 TCM_ALIC_STOR_STARTUPEMPTDSDEACTIV Table 305

TC_0363 TCM_ALIC_STOR_STRTUPEMPTYDSDSACTIV Table 306

TC_0364 TCM_ALIC_STOR_STRTUPDEVREPLACEDSDOWN Table 307

TC_0365 TCM_ALIC_STOR_STRTUPDEVREPLACENODSDOWN Table 308

TC_0366 TCM_ALIC_STOR_STRTUPDEVREPLACEDSUPFLAG Table 309

TC_0367 TCM_ALIC_STOR_STRTUPDEVREPLACEDSFLAGNOUPLOAD Table 310

### Test Specification © IO-Link – 437 – Version V1.1.5

TC_0368 TCM_ALIC_STOR_DSINDLISTCORRUPT Table 311

TC_0369 TCM_ALIC_STOR_DSDOWNLOADFAULT Table 312

TC_0370 TCM_ALIC_STOR_DSUPLOADFAULT Table 313

TC_0371 TCM_ALIC_STOR_INCOMPDEVICE&DS Table 314

TC_0372 TCM_ALIC_STOR_POWEROFFONNONVOLATILEDS Table 315

TC_0373 TCD_DLIC_DEFP_EVENTDEVSTAT Table 319

TC_0374 TCD_DS_APP_UPLOAD_FLAG_NON_VOLATILE Table 321

TC_0375 TCD_DLPC_STDP_MINCYCLETIME_VALID Table 322

TC_0376 TCD_DLPC_OPER_PDVALIDITYINDICATION Table 318

TC_0377 TCD_DLPC_PROP_RECOVERY_TIME Table 317

TC_0378 TCD_APPS_DSUP_INDEXLIST Table 320

3326

3327

Version V1.1.5 – 438 – Test Specification © IO-Link

3328

### 3329 (informative)

### 3330 Information on conformity testing of SDCI

3331 Information about testing Masters and Devices for conformity with [1] and [1]. can be obtained 3332 from the following organization:

3333 IO-Link Community 3334 Haid-und-Neu-Str. 7 3335 76131 Karlsruhe 3336 Germany 3337 Phone: +49 (0) 721 / 96 58 590 3338 Fax: +49 (0) 721 / 96 58 589 3339 E-mail: info@io-link.com 3340 Web site: http://www.io-link.com 3341

3342 Usually, type testing of Master or Device is completed by a manufacturer declaration, which 3343 can be downloaded from the IO-Link website www.io-link.com.

3344

Test Specification © IO-Link – 439 – Version V1.1.5

### 3345 Bibliography

3346 [1] IEC 61131-9 Edition 2 (project in progress), Programmable controllers – Part 9: Single- 3347 drop digital communication interface for small sensors and actuators (SDCI)

3348 [2] ISO/IEC 19505-2:2012 Information technology – OMG Unified Modeling Language 3349 (OMG UML), Revision 2

3350 [3] IO-Link Community, IO Device Description (IODD), V1.1.4, 2024, Order No. 10.012

3351 [4] IO-Link Community, IO-Link Common Profile, V1.2, 2024, Order No. 10.072

3352 [5] IO-Link Community, IO-Link Communication, V1.0, January 2009, Order No. 10.002

3353 [6] IO-Link Community, IO-Link Interface and System, V1.1.5, Order No. 10.002

3354 [7]

3355 [8] IO-Link Community, IO-Link Exceptions, V1.0, January 2020, Order No. 10.232

3356 ____________

3357

Version V1.1.5 – 440 – Test Specification © IO-Link

3358 3359 3360 3361

© Copyright by:

IO-Link Community Ohiostrasse 8 76149 Karlsruhe Germany Phone: +49 (0) 721 / 98 61 97 0 Fax: +49 (0) 721 / 98 61 97 11 e-mail: info@io-link.com http://www.io-link.com/