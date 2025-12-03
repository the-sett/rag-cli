module Main exposing (main)

import Browser
import Css.Global
import Html
import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Html.Styled.Events as HE
import Json.Decode as Decode exposing (Value)
import Main.Style
import Markdown.Render as MdRender exposing (ChatMarkBlock, StreamState)
import Ports
import Procedure.Program
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
    , userInput : String
    , messages : List ChatMessage
    , streamState : StreamState
    , isWaitingForResponse : Bool
    }


type alias ChatMessage =
    { role : String
    , blocks : List ChatMarkBlock
    }


type Msg
    = ProcedureMsg (Procedure.Program.Msg Msg)
    | WsOpened (Result Websocket.Error Websocket.SocketId)
    | WsSent (Result Websocket.Error ())
    | WsClosed (Result Websocket.Error ())
    | WsMessage Websocket.SocketId String
    | WsClosedAsync Websocket.SocketId
    | WsError Websocket.SocketId String
    | UserInputChanged String
    | SendMessage
    | Reconnect



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

        model =
            { procedureState = Procedure.Program.init
            , cragUrl = flags.cragUrl
            , connectionStatus = Connecting
            , userInput = ""
            , messages = []
            , streamState = MdRender.initStreamState
            , isWaitingForResponse = False
            }
    in
    ( model
    , wsApi.open flags.cragUrl WsOpened
    )



-- Update


update : Msg -> Model -> ( Model, Cmd Msg )
update msg model =
    case msg of
        ProcedureMsg procMsg ->
            Procedure.Program.update procMsg model.procedureState
                |> Tuple.mapFirst (\ps -> { model | procedureState = ps })

        WsOpened result ->
            case result of
                Ok socketId ->
                    ( { model | connectionStatus = Connected socketId }
                    , Cmd.none
                    )

                Err _ ->
                    ( { model | connectionStatus = Disconnected }
                    , Cmd.none
                    )

        WsSent _ ->
            ( model, Cmd.none )

        WsClosed _ ->
            ( { model | connectionStatus = Disconnected }
            , Cmd.none
            )

        WsMessage _ payload ->
            handleServerMessage payload model

        WsClosedAsync _ ->
            ( { model | connectionStatus = Disconnected }
            , Cmd.none
            )

        WsError _ _ ->
            ( model, Cmd.none )

        UserInputChanged input ->
            ( { model | userInput = input }, Cmd.none )

        SendMessage ->
            case model.connectionStatus of
                Connected socketId ->
                    if String.trim model.userInput /= "" then
                        let
                            queryJson =
                                "{\"type\":\"query\",\"content\":" ++ encodeString model.userInput ++ "}"

                            newMessage =
                                { role = "user", blocks = [ MdRender.PendingBlock model.userInput ] }
                        in
                        ( { model
                            | userInput = ""
                            , messages = model.messages ++ [ newMessage ]
                            , isWaitingForResponse = True
                            , streamState = MdRender.initStreamState
                          }
                        , wsApi.send socketId queryJson WsSent
                        )

                    else
                        ( model, Cmd.none )

                _ ->
                    ( model, Cmd.none )

        Reconnect ->
            ( { model | connectionStatus = Connecting }
            , wsApi.open model.cragUrl WsOpened
            )


{-| Simple JSON string encoder
-}
encodeString : String -> String
encodeString str =
    "\""
        ++ (str
                |> String.replace "\\" "\\\\"
                |> String.replace "\"" "\\\""
                |> String.replace "\n" "\\n"
                |> String.replace "\r" "\\r"
                |> String.replace "\t" "\\t"
           )
        ++ "\""


{-| Handle incoming server messages
-}
handleServerMessage : String -> Model -> ( Model, Cmd Msg )
handleServerMessage payload model =
    case Decode.decodeString serverMessageDecoder payload of
        Ok serverMsg ->
            case serverMsg of
                DeltaMessage content ->
                    ( { model | streamState = MdRender.feedDelta content model.streamState }
                    , Cmd.none
                    )

                DoneMessage ->
                    let
                        finalBlocks =
                            MdRender.finishStream model.streamState

                        assistantMessage =
                            { role = "assistant", blocks = finalBlocks }
                    in
                    ( { model
                        | messages = model.messages ++ [ assistantMessage ]
                        , streamState = MdRender.initStreamState
                        , isWaitingForResponse = False
                      }
                    , Cmd.none
                    )

                ErrorMessage errorMsg ->
                    let
                        errorChatMessage =
                            { role = "error", blocks = [ MdRender.ErrorBlock "" errorMsg ] }
                    in
                    ( { model
                        | messages = model.messages ++ [ errorChatMessage ]
                        , streamState = MdRender.initStreamState
                        , isWaitingForResponse = False
                      }
                    , Cmd.none
                    )

        Err _ ->
            ( model, Cmd.none )


type ServerMessage
    = DeltaMessage String
    | DoneMessage
    | ErrorMessage String


serverMessageDecoder : Decode.Decoder ServerMessage
serverMessageDecoder =
    Decode.field "type" Decode.string
        |> Decode.andThen
            (\msgType ->
                case msgType of
                    "delta" ->
                        Decode.map DeltaMessage (Decode.field "content" Decode.string)

                    "done" ->
                        Decode.succeed DoneMessage

                    "error" ->
                        Decode.map ErrorMessage (Decode.field "message" Decode.string)

                    _ ->
                        Decode.fail ("Unknown message type: " ++ msgType)
            )



-- Subscriptions


subscriptions : Model -> Sub Msg
subscriptions model =
    Sub.batch
        [ Procedure.Program.subscriptions model.procedureState
        , wsApi.onMessage WsMessage
        , wsApi.onClose WsClosedAsync
        , wsApi.onError WsError
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
        , viewHeader model
        , viewMessages model
        , viewInput model
        ]


viewHeader : Model -> Html Msg
viewHeader model =
    HS.div
        [ HA.class "header" ]
        [ HS.h1
            [ HA.class "header-title" ]
            [ HS.text "CRAG Web Interface" ]
        , viewConnectionStatus model.connectionStatus
        ]


viewConnectionStatus : ConnectionStatus -> Html Msg
viewConnectionStatus status =
    let
        ( statusText, statusClass ) =
            case status of
                Disconnected ->
                    ( "Disconnected", "status-disconnected" )

                Connecting ->
                    ( "Connecting...", "status-connecting" )

                Connected _ ->
                    ( "Connected", "status-connected" )
    in
    HS.div
        [ HA.class "connection-status" ]
        [ HS.div
            [ HA.class "status-indicator"
            , HA.class statusClass
            ]
            []
        , HS.text statusText
        , case status of
            Disconnected ->
                HS.button
                    [ HA.class "reconnect-button"
                    , HE.onClick Reconnect
                    ]
                    [ HS.text "Reconnect" ]

            _ ->
                HS.text ""
        ]


viewMessages : Model -> Html Msg
viewMessages model =
    let
        -- Get pending text from stream state for in-progress rendering
        pendingText =
            MdRender.getPending model.streamState

        -- Add streaming message if there's activity
        streamingMessage =
            if model.isWaitingForResponse then
                [ viewStreamingMessage model.streamState pendingText ]

            else
                []

        allContent =
            List.map viewMessage model.messages ++ streamingMessage
    in
    HS.div
        [ HA.class "messages-container" ]
        (if List.isEmpty model.messages && not model.isWaitingForResponse then
            [ HS.p
                [ HA.class "messages-empty" ]
                [ HS.text "No messages yet. Send a message to start chatting." ]
            ]

         else
            allContent
        )


viewStreamingMessage : StreamState -> String -> Html Msg
viewStreamingMessage streamState pendingText =
    let
        completedBlocks =
            streamState.completedBlocks
    in
    HS.div
        [ HA.class "message"
        , HA.class "message-assistant"
        ]
        [ HS.div
            [ HA.class "message-label" ]
            [ HS.text "Assistant" ]
        , HS.div
            [ HA.class "message-content" ]
            (MdRender.renderBlocks completedBlocks pendingText)
        ]


viewMessage : ChatMessage -> Html Msg
viewMessage message =
    let
        ( roleClass, label ) =
            case message.role of
                "user" ->
                    ( "message-user", "You" )

                "assistant" ->
                    ( "message-assistant", "Assistant" )

                "error" ->
                    ( "message-error", "Error" )

                _ ->
                    ( "message-assistant", message.role )
    in
    HS.div
        [ HA.class "message"
        , HA.class roleClass
        ]
        [ HS.div
            [ HA.class "message-label" ]
            [ HS.text label ]
        , HS.div
            [ HA.class "message-content" ]
            (MdRender.renderBlocks message.blocks "")
        ]


viewInput : Model -> Html Msg
viewInput model =
    let
        isDisabled =
            case model.connectionStatus of
                Connected _ ->
                    model.isWaitingForResponse

                _ ->
                    True

        buttonClass =
            if isDisabled || String.trim model.userInput == "" then
                "send-button-disabled"

            else
                "send-button-enabled"
    in
    HS.div
        [ HA.class "input-container" ]
        [ HS.textarea
            [ HA.class "input-textarea"
            , HA.value model.userInput
            , HE.onInput UserInputChanged
            , HA.placeholder "Type your message..."
            , HA.rows 3
            , HA.disabled isDisabled
            ]
            []
        , HS.button
            [ HA.class "send-button"
            , HA.class buttonClass
            , HE.onClick SendMessage
            , HA.disabled (isDisabled || String.trim model.userInput == "")
            ]
            [ HS.text
                (if model.isWaitingForResponse then
                    "..."

                 else
                    "Send"
                )
            ]
        ]
