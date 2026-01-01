/*
 *    Copyright 2022-2024 mkckr0 <https://github.com/mkckr0>
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

package io.github.mkckr0.audio_share_app.service

import io.github.mkckr0.audio_share_app.model.ProtocolConstants
import io.github.mkckr0.audio_share_app.pb.Client.AudioFormat
import io.github.mkckr0.audio_share_app.service.NetClient.CMD
import io.ktor.network.sockets.BoundDatagramSocket
import io.ktor.network.sockets.ConnectedDatagramSocket
import io.ktor.network.sockets.Datagram
import io.ktor.network.sockets.DatagramReadChannel
import io.ktor.network.sockets.DatagramWriteChannel
import io.ktor.network.sockets.SocketAddress
import io.ktor.utils.io.ByteReadChannel
import io.ktor.utils.io.ByteWriteChannel
import io.ktor.utils.io.core.build
import io.ktor.utils.io.readByteArray
import io.ktor.utils.io.readPacket
import io.ktor.utils.io.writePacket
import kotlinx.io.Buffer
import kotlinx.io.readByteArray
import kotlinx.io.readIntLe
import kotlinx.io.writeIntLe
import java.nio.ByteBuffer

/**
 * Exception thrown when an invalid protocol command is received
 */
class InvalidCommandException(val cmdIndex: Int) : 
    Exception("Invalid command index: $cmdIndex. Valid range: 0-${CMD.entries.size - 1}")

/**
 * Exception thrown when the audio format message size is invalid
 */
class InvalidAudioFormatSizeException(val size: Int) : 
    Exception("Invalid audio format size: $size. Maximum allowed: ${ProtocolConstants.MAX_AUDIO_FORMAT_SIZE}")

suspend fun ByteWriteChannel.writeCMD(cmd: CMD) {
    writePacket(Buffer().apply {
        writeIntLe(cmd.ordinal)
    }.build())
    flush()
}

suspend fun ByteReadChannel.readByteBuffer(count: Int): ByteBuffer {
    return ByteBuffer.wrap(readByteArray(count))
}

suspend fun ByteReadChannel.readIntLE(): Int {
    return readPacket(Int.SIZE_BYTES).readIntLe()
}

/**
 * Read and validate a command from the channel.
 * @throws InvalidCommandException if the command index is out of valid range
 */
suspend fun ByteReadChannel.readCMD(): CMD {
    val cmdIndex = readIntLE()
    if (cmdIndex < 0 || cmdIndex >= CMD.entries.size) {
        throw InvalidCommandException(cmdIndex)
    }
    return CMD.entries[cmdIndex]
}

/**
 * Read and parse an AudioFormat message from the channel.
 * @throws InvalidAudioFormatSizeException if the size exceeds the maximum allowed
 */
suspend fun ByteReadChannel.readAudioFormat(): AudioFormat {
    val size = readIntLE()
    
    // Validate size to prevent OOM attacks
    if (size < 0) {
        throw InvalidAudioFormatSizeException(size)
    }
    if (size > ProtocolConstants.MAX_AUDIO_FORMAT_SIZE) {
        throw InvalidAudioFormatSizeException(size)
    }
    if (size == 0) {
        throw InvalidAudioFormatSizeException(size)
    }
    
    return AudioFormat.parseFrom(readByteBuffer(size))
}

suspend fun ConnectedDatagramSocket.writeIntLE(value: Int) {
    send(Datagram(Buffer().apply {
        this.writeIntLe(value)
    }.build(), remoteAddress))
}

suspend fun DatagramWriteChannel.writeIntLE(value: Int, address: SocketAddress) {
    send(Datagram(Buffer().apply {
        this.writeIntLe(value)
    }.build(), address))
}

suspend fun DatagramReadChannel.readByteBuffer(): ByteBuffer {
    return ByteBuffer.wrap(receive().packet.readByteArray())
}

