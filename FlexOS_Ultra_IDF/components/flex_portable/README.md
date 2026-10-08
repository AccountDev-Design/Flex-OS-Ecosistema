# flex_portable

Módulos de **lógica pura** de la versión Arduino (`../../../FlexOS_Ultra/FlexOS_*.cpp/.h`), copiados
**byte a byte sin cambios**: no dependen de Arduino, de la pantalla ni de la red. `tools/check_portable.py`
falla el build si alguno deja de ser idéntico al original, y las pruebas de host (`tests/host`, con
AddressSanitizer y UBSan) cubren exactamente este código.

| Módulo | Para qué |
|---|---|
| Mem | presupuesto de memoria y política de qué se libera |
| Media, MediaLib, MediaStore, MediaThumb | clasificación de formatos, biblioteca, índice y miniaturas |
| JPEG, JPEGEnc, ImgEdit, VidEdit | JPEG por software, editores de imagen y vídeo |
| FallDetect, Theft | detección de caídas y protección contra robo |
| CloudCore, StorageCore, FlexAuth | núcleo de Flex Cloud, Flex Storage y autenticación |
| AppVM, AppGrant, AppHost | máquina virtual y permisos de las apps de la Store |
| HttpShare, Browser, QR | servidor de subida, núcleo del navegador remoto, QR |
| FlexLink, FlexPhone_Transport | protocolos con el teléfono y Flex Storage |

Pendiente: `PkgCore` incluye `FlexOS_Package.h`, que arrastra `Arduino.h`; se incorporará con la Store.
