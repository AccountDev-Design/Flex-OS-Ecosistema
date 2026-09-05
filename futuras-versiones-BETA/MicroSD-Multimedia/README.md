# MicroSD + Multimedia (BETA archivada)

Este directorio conserva la implementacion experimental de microSD para una
futura version de Flex OS Ultra. **No forma parte del firmware activo**, no se
incluye desde `FlexOS_Ultra.ino` y no debe compilarse junto con los archivos de
la raiz.

La integracion completa anterior puede consultarse en el commit `cdcc464`. El
controlador, las opciones de compilacion y la documentacion de hardware se
guardan aqui para investigarlos sin volver a introducir sondeos SDMMC en el
arranque o en el bucle principal.

Para reactivarla en el futuro sera necesario validar en placa, como un trabajo
separado, la convivencia del controlador SDMMC con el transporte Wi-Fi del C6.
No se debe copiar este directorio a la raiz sin repetir pruebas de arranque,
conexion automatica Wi-Fi, estabilidad prolongada y extraccion en caliente.

La version activa usa exclusivamente LittleFS para archivos, Galeria,
Multimedia y Almacenamiento.
