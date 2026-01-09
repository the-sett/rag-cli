port module Ports exposing (websocketPorts, pushUrl, onUrlChange, debugLog, copyToClipboard)

import Json.Decode exposing (Value)
import Websocket



-- Ports for Navigation


port pushUrl : String -> Cmd msg


port onUrlChange : (String -> msg) -> Sub msg


port debugLog : String -> Cmd msg


port copyToClipboard : String -> Cmd msg



-- Ports for WebSocket communication


port wsOpen : { id : String, url : String } -> Cmd msg


port wsSend : { id : String, socketId : String, payload : String } -> Cmd msg


port wsClose : { id : String, socketId : String } -> Cmd msg


port wsResponse : ({ id : String, type_ : String, socketId : String, response : Value } -> msg) -> Sub msg


port wsOnMessage : ({ socketId : String, payload : String } -> msg) -> Sub msg


port wsOnClose : (String -> msg) -> Sub msg


port wsOnError : ({ socketId : String, error : String } -> msg) -> Sub msg



-- Wire up ports to the Websocket module


websocketPorts : Websocket.Ports msg
websocketPorts =
    { open = wsOpen
    , send = wsSend
    , close = wsClose
    , response = wsResponse
    , onMessage = wsOnMessage
    , onClose = wsOnClose
    , onError = wsOnError
    }
