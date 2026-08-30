package com.exelite.engine

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.util.Log
import java.io.InputStream
import java.io.OutputStream
import java.net.ServerSocket
import java.net.Socket
import kotlin.concurrent.thread

class X11TcpProxy(private val unixSocketPath: String, private val tcpPort: Int = 6000) {
    private val TAG = "X11TcpProxy"
    private var serverSocket: ServerSocket? = null
    private var isRunning = false

    fun start() {
        if (isRunning) return
        isRunning = true
        thread(name = "X11TcpProxy-Acceptor") {
            try {
                serverSocket = ServerSocket(tcpPort)
                Log.i(TAG, "TCP proxy listening on port $tcpPort -> forwarding to $unixSocketPath")
                while (isRunning) {
                    val tcpSocket = serverSocket?.accept() ?: break
                    Log.i(TAG, "New X11 connection from ${tcpSocket.inetAddress}")
                    handleConnection(tcpSocket)
                }
            } catch (e: Exception) {
                if (isRunning) Log.e(TAG, "Proxy accept error", e)
            }
        }
    }

    private fun handleConnection(tcpSocket: Socket) {
        thread {
            var localSocket: LocalSocket? = null
            try {
                localSocket = LocalSocket()
                localSocket.connect(LocalSocketAddress(unixSocketPath, LocalSocketAddress.Namespace.FILESYSTEM))
                
                val tcpIn = tcpSocket.inputStream
                val tcpOut = tcpSocket.outputStream
                val unixIn = localSocket.inputStream
                val unixOut = localSocket.outputStream

                // TCP -> Unix
                thread {
                    try {
                        pump(tcpIn, unixOut)
                    } catch (e: Exception) {}
                    tcpSocket.close()
                    localSocket.close()
                }

                // Unix -> TCP
                try {
                    pump(unixIn, tcpOut)
                } catch (e: Exception) {}
                
            } catch (e: Exception) {
                Log.e(TAG, "Failed to connect to Unix socket or forward", e)
            } finally {
                tcpSocket.close()
                localSocket?.close()
            }
        }
    }

    private fun pump(input: InputStream, output: OutputStream) {
        val buffer = ByteArray(8192)
        var bytesRead: Int
        while (input.read(buffer).also { bytesRead = it } != -1) {
            output.write(buffer, 0, bytesRead)
            output.flush()
        }
    }

    fun stop() {
        isRunning = false
        try {
            serverSocket?.close()
        } catch (e: Exception) {}
    }
}
