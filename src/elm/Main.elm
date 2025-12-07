module Main exposing (main)

import Browser
import Css.Global
import Html
import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Json.Decode as Decode exposing (Value)
import Main.Style
import Navigation exposing (Route)
import Pages.Chat.Component as Chat
import Pages.Intro.Component as Intro
import Ports
import Procedure.Program
import Update2 as U2
import Websocket



-- Flags


type alias Flags =
    { cragUrl : String
    }


flagsDecoder : Decode.Decoder Flags
flagsDecoder =
    Decode.map Flags
        (Decode.field "cragUrl" Decode.string)



-- Main


main : Program Value Model Msg
main =
    Browser.element
        { init = init
        , update = update
        , view = view
        , subscriptions = subscriptions
        }



-- Model


type ConnectionStatus
    = Disconnected
    | Connecting
    | Connected Websocket.SocketId


type alias Model =
    { procedureState : Procedure.Program.Model Msg
    , cragUrl : String
    , connectionStatus : ConnectionStatus
    , route : Maybe Route
    , intro : Intro.Model
    , chat : Chat.Model
    }


type Msg
    = ProcedureMsg (Procedure.Program.Msg Msg)
    | WsOpened (Result Websocket.Error Websocket.SocketId)
    | WsSent (Result Websocket.Error ())
    | WsClosed (Result Websocket.Error ())
    | WsMessage Websocket.SocketId String
    | WsClosedAsync Websocket.SocketId
    | WsError Websocket.SocketId String
    | UrlChanged (Maybe Route)
    | Reconnect
    | IntroMsg Intro.Msg
    | ChatMsg Chat.Msg



-- WebSocket API helper


wsApi : Websocket.WebsocketApi Msg
wsApi =
    Websocket.websocketApi ProcedureMsg Ports.websocketPorts



-- Init


init : Value -> ( Model, Cmd Msg )
init flagsValue =
    let
        flags =
            Decode.decodeValue flagsDecoder flagsValue
                |> Result.withDefault { cragUrl = "ws://localhost:8193" }

        ( introModel, _ ) =
            Intro.init

        ( chatModel, _ ) =
            Chat.init

        model =
            { procedureState = Procedure.Program.init
            , cragUrl = flags.cragUrl
            , connectionStatus = Connecting
            , route = Just Navigation.Intro
            , intro = introModel
            , chat = chatModel
            }
    in
    ( model
    , wsApi.open flags.cragUrl WsOpened
    )



-- Protocols


introProtocol : Model -> Intro.Protocol Model Msg
introProtocol model =
    { toMsg = IntroMsg
    , onUpdate = \( introModel, cmds ) -> ( { model | intro = introModel }, cmds )
    , onReady = \( introModel, cmds ) ->
        ( { model | intro = introModel }
        , Cmd.batch [ cmds, Navigation.pushUrl (Navigation.routeToString Navigation.Chat) ]
        )
    }


chatProtocol : Model -> Chat.Protocol Model Msg
chatProtocol model =
    { toMsg = ChatMsg
    , onUpdate = \( chatModel, cmds ) -> ( { model | chat = chatModel }, cmds )
    , onSendMessage = \text ( chatModel, cmds ) ->
        case model.connectionStatus of
            Connected socketId ->
                let
                    queryJson =
                        "{\"type\":\"query\",\"content\":" ++ encodeString text ++ "}"
                in
                ( { model | chat = chatModel }
                , Cmd.batch [ cmds, wsApi.send socketId queryJson WsSent ]
                )

            _ ->
                ( { model | chat = chatModel }, cmds )
    }



-- Update


update : Msg -> Model -> ( Model, Cmd Msg )
update msg model =
    case msg of
        ProcedureMsg procMsg ->
            let
                updateProcedureState ps =
                    { model | procedureState = ps }
            in
            Procedure.Program.update procMsg model.procedureState
                |> Tuple.mapFirst updateProcedureState

        WsOpened result ->
            handleWsOpened result model

        WsSent _ ->
            U2.pure model

        WsClosed _ ->
            setDisconnected model

        WsMessage _ payload ->
            handleServerMessage payload model

        WsClosedAsync _ ->
            setDisconnected model

        WsError _ _ ->
            U2.pure model

        UrlChanged route ->
            U2.pure { model | route = route }

        Reconnect ->
            U2.pure { model | connectionStatus = Connecting }
                |> U2.andThen reconnectWebSocket

        IntroMsg introMsg ->
            Intro.update (introProtocol model) introMsg model.intro

        ChatMsg chatMsg ->
            Chat.update (chatProtocol model) chatMsg model.chat



-- Update Helpers


handleWsOpened : Result Websocket.Error Websocket.SocketId -> Model -> ( Model, Cmd Msg )
handleWsOpened result model =
    case result of
        Ok socketId ->
            U2.pure { model | connectionStatus = Connected socketId }

        Err _ ->
            U2.pure { model | connectionStatus = Disconnected }


setDisconnected : Model -> ( Model, Cmd Msg )
setDisconnected model =
    U2.pure { model | connectionStatus = Disconnected }


reconnectWebSocket : Model -> ( Model, Cmd Msg )
reconnectWebSocket model =
    ( model, wsApi.open model.cragUrl WsOpened )


encodeString : String -> String
encodeString str =
    "\""
        ++ (str
                |> String.replace "\\" "\\\\"
                |> String.replace "\"" "\\\""
                |> String.replace "\n" "\\n"
                |> String.replace "\u{000D}" "\\r"
                |> String.replace "\t" "\\t"
           )
        ++ "\""


handleServerMessage : String -> Model -> ( Model, Cmd Msg )
handleServerMessage payload model =
    case Decode.decodeString serverMessageDecoder payload of
        Ok serverMsg ->
            case serverMsg of
                DeltaMessage content ->
                    Chat.receiveStreamDelta (chatProtocol model) content model.chat

                DoneMessage ->
                    Chat.receiveStreamDone (chatProtocol model) model.chat

                ErrorMessage errorMsg ->
                    Chat.receiveStreamError (chatProtocol model) errorMsg model.chat

        Err _ ->
            U2.pure model


type ServerMessage
    = DeltaMessage String
    | DoneMessage
    | ErrorMessage String


serverMessageDecoder : Decode.Decoder ServerMessage
serverMessageDecoder =
    let
        decodeByType msgType =
            case msgType of
                "delta" ->
                    Decode.map DeltaMessage (Decode.field "content" Decode.string)

                "done" ->
                    Decode.succeed DoneMessage

                "error" ->
                    Decode.map ErrorMessage (Decode.field "message" Decode.string)

                _ ->
                    Decode.fail ("Unknown message type: " ++ msgType)
    in
    Decode.field "type" Decode.string
        |> Decode.andThen decodeByType



-- Subscriptions


subscriptions : Model -> Sub Msg
subscriptions model =
    Sub.batch
        [ Procedure.Program.subscriptions model.procedureState
        , wsApi.onMessage WsMessage
        , wsApi.onClose WsClosedAsync
        , wsApi.onError WsError
        , Navigation.onUrlChange (Navigation.locationHrefToRoute >> UrlChanged)
        ]



-- View


view : Model -> Html.Html Msg
view model =
    viewStyled model |> HS.toUnstyled


viewStyled : Model -> Html Msg
viewStyled model =
    HS.div
        [ HA.class "app-container" ]
        [ Main.Style.style |> Css.Global.global
        , viewPage model
        ]


viewPage : Model -> Html Msg
viewPage model =
    case model.route of
        Just Navigation.Intro ->
            Intro.view { toMsg = IntroMsg }

        Just Navigation.Chat ->
            Chat.view
                { toMsg = ChatMsg
                , isConnected = isConnected model.connectionStatus
                , onReconnect = Reconnect
                }
                model.chat

        Nothing ->
            Intro.view { toMsg = IntroMsg }


isConnected : ConnectionStatus -> Bool
isConnected status =
    case status of
        Connected _ ->
            True

        _ ->
            False
