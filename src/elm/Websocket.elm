module Websocket exposing
    ( Ports
    , WebsocketApi, websocketApi
    , SocketId
    , Error, errorToString
    )

{-| A WebSocket API using elm-procedure for request-response correlation.


# Ports

@docs Ports


# Packaged API

@docs WebsocketApi, websocketApi
@docs SocketId


# Error reporting

@docs Error, errorToString

-}

import Json.Decode as Decode exposing (Value)
import Procedure
import Procedure.Channel as Channel
import Procedure.Program



-- API


{-| The ports that need to be wired up to websockets.ts
-}
type alias Ports msg =
    { open : { id : String, url : String } -> Cmd msg
    , send : { id : String, socketId : String, payload : String } -> Cmd msg
    , close : { id : String, socketId : String } -> Cmd msg
    , response : ({ id : String, type_ : String, socketId : String, response : Value } -> msg) -> Sub msg
    , onMessage : ({ socketId : String, payload : String } -> msg) -> Sub msg
    , onClose : (String -> msg) -> Sub msg
    , onError : ({ socketId : String, error : String } -> msg) -> Sub msg
    }


{-| The WebSocket API.
-}
type alias WebsocketApi msg =
    { open : String -> (Result Error SocketId -> msg) -> Cmd msg
    , send : SocketId -> String -> (Result Error () -> msg) -> Cmd msg
    , close : SocketId -> (Result Error () -> msg) -> Cmd msg
    , onMessage : (SocketId -> String -> msg) -> Sub msg
    , onClose : (SocketId -> msg) -> Sub msg
    , onError : (SocketId -> String -> msg) -> Sub msg
    }


{-| Creates an instance of the WebSocket API.
-}
websocketApi : (Procedure.Program.Msg msg -> msg) -> Ports msg -> WebsocketApi msg
websocketApi pt ports =
    { open = open pt ports
    , send = send pt ports
    , close = close pt ports
    , onMessage = onMessage ports
    , onClose = onClose ports
    , onError = onError ports
    }


{-| An opaque identifier for an open WebSocket connection.
-}
type SocketId
    = SocketId String


{-| Possible errors arising from WebSocket operations.
-}
type Error
    = WebsocketError String


{-| Turns WebSocket errors into strings.
-}
errorToString : Error -> String
errorToString (WebsocketError msg) =
    msg



-- Implementation


decodeResponse : { a | type_ : String, response : Value } -> Result Error ()
decodeResponse res =
    case res.type_ of
        "Ok" ->
            Ok ()

        "Error" ->
            res.response
                |> Decode.decodeValue Decode.string
                |> Result.withDefault "Unknown error"
                |> WebsocketError
                |> Err

        _ ->
            WebsocketError ("Unknown response type: " ++ res.type_)
                |> Err


decodeResponseWithSocketId : { a | type_ : String, socketId : String, response : Value } -> Result Error SocketId
decodeResponseWithSocketId res =
    case res.type_ of
        "Ok" ->
            SocketId res.socketId |> Ok

        "Error" ->
            res.response
                |> Decode.decodeValue Decode.string
                |> Result.withDefault "Unknown error"
                |> WebsocketError
                |> Err

        _ ->
            WebsocketError ("Unknown response type: " ++ res.type_)
                |> Err


open :
    (Procedure.Program.Msg msg -> msg)
    -> Ports msg
    -> String
    -> (Result Error SocketId -> msg)
    -> Cmd msg
open pt ports url toMsg =
    Channel.open (\key -> ports.open { id = key, url = url })
        |> Channel.connect ports.response
        |> Channel.filter (\key { id } -> id == key)
        |> Channel.acceptOne
        |> Procedure.run pt (\res -> decodeResponseWithSocketId res |> toMsg)


send :
    (Procedure.Program.Msg msg -> msg)
    -> Ports msg
    -> SocketId
    -> String
    -> (Result Error () -> msg)
    -> Cmd msg
send pt ports (SocketId socketId) payload toMsg =
    Channel.open (\key -> ports.send { id = key, socketId = socketId, payload = payload })
        |> Channel.connect ports.response
        |> Channel.filter (\key { id } -> id == key)
        |> Channel.acceptOne
        |> Procedure.run pt (\res -> decodeResponse res |> toMsg)


close :
    (Procedure.Program.Msg msg -> msg)
    -> Ports msg
    -> SocketId
    -> (Result Error () -> msg)
    -> Cmd msg
close pt ports (SocketId socketId) toMsg =
    Channel.open (\key -> ports.close { id = key, socketId = socketId })
        |> Channel.connect ports.response
        |> Channel.filter (\key { id } -> id == key)
        |> Channel.acceptOne
        |> Procedure.run pt (\res -> decodeResponse res |> toMsg)


{-| Subscribe to incoming WebSocket messages.
-}
onMessage : Ports msg -> (SocketId -> String -> msg) -> Sub msg
onMessage ports toMsg =
    ports.onMessage (\{ socketId, payload } -> toMsg (SocketId socketId) payload)


{-| Subscribe to WebSocket close events.
-}
onClose : Ports msg -> (SocketId -> msg) -> Sub msg
onClose ports toMsg =
    ports.onClose (\socketId -> toMsg (SocketId socketId))


{-| Subscribe to WebSocket error events.
-}
onError : Ports msg -> (SocketId -> String -> msg) -> Sub msg
onError ports toMsg =
    ports.onError (\{ socketId, error } -> toMsg (SocketId socketId) error)
