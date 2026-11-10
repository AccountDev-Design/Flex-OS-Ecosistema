# Certificado raíz del servidor de actualizaciones

Coloca aquí `ota_root_ca.pem` (formato PEM, una o varias raíces concatenadas) para FIJAR en qué CA confía la OTA. Sin ese archivo el firmware **no se
conecta** al servidor de actualizaciones y Ajustes dice "Falta el certificado del servidor": es a propósito (idéntico a Arduino con
`FLEXOS_OTA_ROOT_CA` vacío). Hay que fijar la cadena de **todos** los hosts por los que pasa la descarga (el manifiesto en `raw.githubusercontent.com` y,
si redirige, el host de los binarios), y renovarla cuando esa CA caduque.

No se incluye ninguna raíz en el repositorio: decide el propietario del servidor de actualizaciones.
