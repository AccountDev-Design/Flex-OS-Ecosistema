package com.flexos.flexphone.cloud

/**
 * Errores de la API: un codigo ESTABLE (lo leen el P4 y la web) y un mensaje
 * para personas. Son los MISMOS codigos que el servicio de Flex Cloud de Flex
 * Developer Studio (`cloud/src/http/errors.js`): el gestor del P4 decide por
 * ellos si reintenta, espera o se rinde, asi que no se inventan otros.
 */
class CloudError(
    val status: Int,
    val code: String,
    message: String,
    val details: Map<String, Any?>? = null,
) : RuntimeException(message)

object E {
    fun authRequired() = CloudError(401, "auth_required", "La sesión de Flex OS no es válida o caducó. Flex OS abre otra sola.")
    fun unknownDevice() = CloudError(403, "device_revoked", "Este teléfono no está emparejado con ese Flex OS.")
    fun forbiddenNetwork() = CloudError(403, "forbidden", "Flex Cloud del teléfono solo atiende a la red local.")
    fun notFound(what: String = "El elemento") = CloudError(404, "not_found", "$what no existe.")
    fun invalid(msg: String, details: Map<String, Any?>? = null) = CloudError(400, "invalid_request", msg, details)
    fun nameInvalid(msg: String) = CloudError(400, "name_invalid", msg)
    fun nameConflict(name: String) = CloudError(409, "name_conflict", "Ya existe un elemento llamado \"$name\" en esta carpeta.")
    fun quotaExceeded(needed: Long, available: Long) = CloudError(
        507, "quota_exceeded", "No queda espacio suficiente en tu Flex Cloud.",
        mapOf("needed" to needed, "available" to available),
    )
    fun fileTooLarge(max: Long) = CloudError(413, "file_too_large", "El archivo supera el tamaño máximo permitido.", mapOf("max" to max))
    fun payloadTooLarge() = CloudError(413, "payload_too_large", "La solicitud es demasiado grande.")
    fun uploadNotFound() = CloudError(404, "upload_not_found", "La subida no existe o ya terminó.")
    fun uploadExpired() = CloudError(410, "upload_expired", "La subida caducó. Empieza de nuevo.")
    fun uploadState(state: String) = CloudError(409, "upload_state", "La subida no admite esa operación (estado: $state).", mapOf("state" to state))
    fun partRange(total: Int) = CloudError(400, "part_out_of_range", "La parte no existe (la subida tiene $total).", mapOf("total" to total))
    fun partSize(expected: Long, got: Long) = CloudError(400, "part_size_mismatch", "La parte no tiene el tamaño esperado.", mapOf("expected" to expected, "got" to got))
    fun checksum(expected: String, got: String) = CloudError(
        422, "checksum_mismatch", "Los datos llegaron alterados (SHA-256 distinto). Se reintentará.",
        mapOf("expected" to expected, "got" to got),
    )
    fun partConflict() = CloudError(409, "part_conflict", "Esa parte ya se recibió con otro contenido.")
    fun incomplete(missing: List<Int>) = CloudError(409, "incomplete_upload", "Faltan partes por subir.", mapOf("missing" to missing))
    fun range(size: Long) = CloudError(416, "range_not_satisfiable", "El rango pedido no existe en el archivo.", mapOf("size" to size))
    fun rateLimited() = CloudError(429, "rate_limited", "Demasiadas solicitudes. Espera un momento.")
    fun folderCycle() = CloudError(400, "folder_cycle", "No puedes mover una carpeta dentro de sí misma.")
    fun linkInvalid() = CloudError(403, "link_invalid", "El enlace no es válido o caducó.")
    fun methodNotAllowed() = CloudError(405, "method_not_allowed", "Método no permitido.")
    fun busy() = CloudError(503, "server_busy", "Flex Cloud del teléfono está atendiendo otras transferencias. Inténtalo en unos segundos.")
    fun lengthRequired() = CloudError(411, "invalid_request", "Falta la longitud del cuerpo (Content-Length).")
    fun noSpaceDevice(available: Long) = CloudError(
        507, "quota_exceeded", "El teléfono no tiene espacio libre suficiente.",
        mapOf("available" to available),
    )
}

/** Cuerpo JSON de un error ({ok:false, error:{code, message, details?}}). */
fun errorBody(err: Throwable): Pair<Int, Map<String, Any?>> {
    if (err is CloudError) {
        val e = LinkedHashMap<String, Any?>()
        e["code"] = err.code
        e["message"] = err.message
        if (err.details != null) e["details"] = err.details
        return err.status to mapOf("ok" to false, "error" to e)
    }
    return 500 to mapOf(
        "ok" to false,
        "error" to mapOf("code" to "internal_error", "message" to "Error interno de Flex Cloud del teléfono. Vuelve a intentarlo."),
    )
}
