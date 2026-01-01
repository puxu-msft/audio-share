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

package io.github.mkckr0.audio_share_app.model

import android.annotation.SuppressLint
import android.app.NotificationManager
import kotlin.time.Duration.Companion.seconds

enum class WorkName(val value: String)
{
    AUTO_CHECK_UPDATE("auto_check_update"),
    CHECK_UPDATE("check_update"),
}

enum class Channel(val id: String, val title: String, val importance: Int) {
    @SuppressLint("InlinedApi")
    UPDATE("CHANNEL_ID_UPDATE", "Update", NotificationManager.IMPORTANCE_DEFAULT),
}

enum class Notification(val id: Int) {
    UPDATE(1)
}

/**
 * Network protocol constants
 */
object ProtocolConstants {
    /** Default server port */
    const val DEFAULT_PORT = 65530
    
    /** Minimum valid port number */
    const val MIN_PORT = 1
    
    /** Maximum valid port number */
    const val MAX_PORT = 65535
    
    /** Maximum size of AudioFormat protobuf message in bytes */
    const val MAX_AUDIO_FORMAT_SIZE = 1024
    
    /** TCP connection timeout */
    val CONNECTION_TIMEOUT = 3.seconds
    
    /** Heartbeat interval */
    val HEARTBEAT_INTERVAL = 3.seconds
    
    /** Heartbeat timeout */
    val HEARTBEAT_TIMEOUT = 5.seconds
}