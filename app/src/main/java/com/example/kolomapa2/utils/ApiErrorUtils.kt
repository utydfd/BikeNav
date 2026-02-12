package com.example.kolomapa2.utils

import java.io.InterruptedIOException
import java.io.IOException
import java.net.ConnectException
import java.net.SocketTimeoutException
import java.net.UnknownHostException

class ApiHttpException(
    val service: String,
    val code: Int,
    val body: String? = null
) : IOException(buildString {
    append(service)
    append(" error: ")
    append(code)
    if (!body.isNullOrBlank()) {
        append(" - ")
        append(body.take(120))
    }
})

object ApiErrorUtils {
    fun toUserMessage(error: Throwable, fallback: String = "Request failed"): String {
        val root = rootCause(error)
        return when (root) {
            is ApiHttpException -> httpCodeMessage(root.code) ?: "${root.service} error (${root.code})"
            is UnknownHostException, is ConnectException -> "No internet connection"
            is SocketTimeoutException, is InterruptedIOException -> "Network timeout"
            else -> root.message?.take(60) ?: fallback
        }
    }

    fun httpCodeMessage(code: Int): String? = when (code) {
        401, 403 -> "Access denied"
        404 -> "Resource not found"
        429 -> "Rate limited"
        in 500..599 -> "Service unavailable"
        else -> null
    }

    private fun rootCause(error: Throwable): Throwable {
        var current = error
        while (current.cause != null && current.cause !== current) {
            current = current.cause!!
        }
        return current
    }
}
