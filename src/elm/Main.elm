module Main exposing (main)

import Browser
import Browser.Events
import Css.Global
import Html
import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Html.Styled.Events as HE
import Http
import Json.Decode as Decode exposing (Value)
import Main.Style
import Navigation exposing (Route)
import Pages.Agents.Component as Agents
import Pages.Chat.Component as Chat
import Pages.Intro.Component as Intro
import Ports
import Procedure.Program
import Settings exposing (AppSettings, SubmitShortcut)
import Update2 as U2
import Websocket



-- Flags


type alias Flags =
    { cragUrl : String
    , locationHref : String
    }


flagsDecoder : Decode.Decoder Flags
flagsDecoder =
    Decode.map2 Flags
        (Decode.field "cragUrl" Decode.string)
        (Decode.field "locationHref" Decode.string)



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
    , agents : Agents.Model
    , pendingChatInit : Maybe String  -- Chat ID to send on init (Nothing = new chat, Just "" = needs init, Just id = reconnect)
    , pendingAgentId : Maybe String   -- Agent ID to use when starting a new chat with an agent
    , sessionInitialized : Bool       -- Whether we've sent an init message for current session
    , appSettings : AppSettings       -- Application settings (submit shortcut, etc.)
    , showSettingsModal : Bool        -- Whether the settings modal is visible
    , pendingSettings : AppSettings   -- Settings being edited in the modal (before save)
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
    | GoHome
    | GlobalKeyDown String
    | IntroMsg Intro.Msg
    | ChatMsg Chat.Msg
    | AgentsMsg Agents.Msg
    | GotSettings (Result Http.Error AppSettings)
    | OpenSettingsModal
    | CloseSettingsModal
    | SetPendingShortcut SubmitShortcut
    | SaveSettings
    | SettingsSaved (Result Http.Error AppSettings)
    | NoOp



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
                |> Result.withDefault { cragUrl = "ws://localhost:8193", locationHref = "" }

        -- Parse initial route from URL
        initialRoute =
            Navigation.locationHrefToRoute flags.locationHref
                |> Maybe.withDefault Navigation.Intro

        -- Extract chat ID if navigating directly to a chat
        ( initialChatId, pendingInit ) =
            case initialRoute of
                Navigation.Chat maybeChatId ->
                    ( maybeChatId, maybeChatId )

                _ ->
                    ( Nothing, Nothing )

        ( introModel, _ ) =
            Intro.init

        ( chatModel, _ ) =
            Chat.init initialChatId

        ( agentsModel, _ ) =
            Agents.init

        model =
            { procedureState = Procedure.Program.init
            , cragUrl = flags.cragUrl
            , connectionStatus = Connecting
            , route = Just initialRoute
            , intro = introModel
            , chat = chatModel
            , agents = agentsModel
            , pendingChatInit = pendingInit
            , pendingAgentId = Nothing
            , sessionInitialized = False
            , appSettings = Settings.defaultSettings
            , showSettingsModal = False
            , pendingSettings = Settings.defaultSettings
            }
    in
    ( model
    , Cmd.batch
        [ wsApi.open flags.cragUrl WsOpened
        , Intro.fetchChats IntroMsg
        , Intro.fetchAgents IntroMsg
        , Settings.fetchSettings GotSettings
        ]
    )



-- Protocols


introProtocol : Model -> Intro.Protocol Model Msg
introProtocol model =
    { toMsg = IntroMsg
    , onUpdate = \( introModel, cmds ) -> ( { model | intro = introModel }, cmds )
    , onReady = \( introModel, cmds ) ->
        let
            -- Reset chat for new conversation
            ( newChatModel, _ ) =
                Chat.init Nothing
        in
        ( { model
            | intro = introModel
            , chat = newChatModel
            , pendingChatInit = Just ""  -- Empty string means new chat
            , pendingAgentId = Nothing   -- No agent for regular chat
            , sessionInitialized = False
          }
        , Cmd.batch [ cmds, Navigation.pushUrl (Navigation.routeToString (Navigation.Chat Nothing)) ]
        )
    , onSelectChat = \chatId ( introModel, cmds ) ->
        let
            -- Initialize chat with existing chat ID
            ( newChatModel, _ ) =
                Chat.init (Just chatId)
        in
        ( { model
            | intro = introModel
            , chat = newChatModel
            , pendingChatInit = Just chatId  -- Reconnect to existing chat
            , pendingAgentId = Nothing       -- Agent ID is stored with the chat on server
            , sessionInitialized = False
          }
        , Cmd.batch [ cmds, Navigation.pushUrl (Navigation.routeToString (Navigation.Chat (Just chatId))) ]
        )
    , onSelectAgentChat = \agentId ( introModel, cmds ) ->
        let
            -- Start new chat with agent
            ( newChatModel, _ ) =
                Chat.init Nothing
        in
        ( { model
            | intro = introModel
            , chat = newChatModel
            , pendingChatInit = Just ""      -- New chat
            , pendingAgentId = Just agentId  -- Agent to use for this chat
            , sessionInitialized = False
          }
        , Cmd.batch [ cmds, Navigation.pushUrl (Navigation.routeToString (Navigation.Chat Nothing)) ]
        )
    , onGoToAgents = \( introModel, cmds ) ->
        ( { model | intro = introModel }
        , Cmd.batch [ cmds, Navigation.pushUrl (Navigation.routeToString Navigation.Agents) ]
        )
    , onOpenSettings = \( introModel, cmds ) ->
        ( { model
            | intro = introModel
            , showSettingsModal = True
            , pendingSettings = model.appSettings
          }
        , cmds
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
    , onCancelStream = \( chatModel, cmds ) ->
        case model.connectionStatus of
            Connected socketId ->
                let
                    cancelJson =
                        "{\"type\":\"cancel\"}"
                in
                ( { model | chat = chatModel }
                , Cmd.batch [ cmds, wsApi.send socketId cancelJson WsSent, Ports.debugLog "Sending cancel WebSocket message" ]
                )

            _ ->
                ( { model | chat = chatModel }, Cmd.batch [ cmds, Ports.debugLog "Not connected - cannot send cancel" ] )
    }


agentsProtocol : Model -> Agents.Protocol Model Msg
agentsProtocol model =
    { toMsg = AgentsMsg
    , onUpdate = \( agentsModel, cmds ) -> ( { model | agents = agentsModel }, cmds )
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
            handleUrlChanged route model

        Reconnect ->
            U2.pure { model | connectionStatus = Connecting, sessionInitialized = False }
                |> U2.andThen reconnectWebSocket

        GoHome ->
            ( model, Navigation.pushUrl (Navigation.routeToString Navigation.Intro) )

        GlobalKeyDown key ->
            -- Handle Escape key to cancel streaming (only on chat page while waiting)
            if key == "Escape" then
                if model.chat.isWaitingForResponse then
                    let
                        ( newModel, cmds ) =
                            Chat.update (chatProtocol model) Chat.cancelStreamMsg model.chat
                    in
                    ( newModel, Cmd.batch [ cmds, Ports.debugLog "Escape pressed - cancelling stream" ] )

                else
                    ( model, Ports.debugLog "Escape pressed - not waiting" )

            else
                -- Log all key presses for debugging
                ( model, Ports.debugLog ("Key pressed: " ++ key) )

        IntroMsg introMsg ->
            Intro.update (introProtocol model) introMsg model.intro

        ChatMsg chatMsg ->
            Chat.update (chatProtocol model) chatMsg model.chat

        AgentsMsg agentsMsg ->
            Agents.update (agentsProtocol model) agentsMsg model.agents

        GotSettings result ->
            case result of
                Ok settings ->
                    U2.pure { model | appSettings = settings, pendingSettings = settings }

                Err _ ->
                    -- Keep defaults if we fail to load settings
                    U2.pure model

        OpenSettingsModal ->
            U2.pure
                { model
                    | showSettingsModal = True
                    , pendingSettings = model.appSettings
                }

        CloseSettingsModal ->
            U2.pure { model | showSettingsModal = False }

        SetPendingShortcut shortcut ->
            let
                pending =
                    model.pendingSettings
            in
            U2.pure { model | pendingSettings = { pending | submitShortcut = shortcut } }

        SaveSettings ->
            ( model, Settings.saveSettings SettingsSaved model.pendingSettings )

        SettingsSaved result ->
            case result of
                Ok settings ->
                    U2.pure
                        { model
                            | appSettings = settings
                            , pendingSettings = settings
                            , showSettingsModal = False
                        }

                Err _ ->
                    -- Close modal even on error (settings will revert)
                    U2.pure { model | showSettingsModal = False }

        NoOp ->
            U2.pure model



-- Update Helpers


handleWsOpened : Result Websocket.Error Websocket.SocketId -> Model -> ( Model, Cmd Msg )
handleWsOpened result model =
    case result of
        Ok socketId ->
            let
                newModel =
                    { model | connectionStatus = Connected socketId }
            in
            -- If we have a pending chat init and we're on the chat page, send it now
            case ( model.pendingChatInit, model.route ) of
                ( Just chatId, Just (Navigation.Chat _) ) ->
                    sendInitMessage socketId chatId newModel

                _ ->
                    U2.pure newModel

        Err _ ->
            U2.pure { model | connectionStatus = Disconnected }


sendInitMessage : Websocket.SocketId -> String -> Model -> ( Model, Cmd Msg )
sendInitMessage socketId chatId model =
    let
        initJson =
            if not (String.isEmpty chatId) then
                -- Reconnecting to existing chat - just send chat_id
                "{\"type\":\"init\",\"chat_id\":" ++ encodeString chatId ++ "}"

            else
                -- New chat - may include agent_id
                case model.pendingAgentId of
                    Just agentId ->
                        "{\"type\":\"init\",\"agent_id\":" ++ encodeString agentId ++ "}"

                    Nothing ->
                        "{\"type\":\"init\"}"
    in
    ( { model | sessionInitialized = True, pendingChatInit = Nothing, pendingAgentId = Nothing }
    , wsApi.send socketId initJson WsSent
    )


setDisconnected : Model -> ( Model, Cmd Msg )
setDisconnected model =
    U2.pure { model | connectionStatus = Disconnected, sessionInitialized = False }


reconnectWebSocket : Model -> ( Model, Cmd Msg )
reconnectWebSocket model =
    ( model, wsApi.open model.cragUrl WsOpened )


handleUrlChanged : Maybe Route -> Model -> ( Model, Cmd Msg )
handleUrlChanged route model =
    let
        baseModel =
            { model | route = route }
    in
    case route of
        Just Navigation.Intro ->
            -- Fetch chats and agents when navigating to Intro page
            ( baseModel
            , Cmd.batch
                [ Intro.fetchChats IntroMsg
                , Intro.fetchAgents IntroMsg
                ]
            )

        Just (Navigation.Chat maybeChatId) ->
            -- When navigating to chat, send init if connected and not already initialized
            let
                chatId =
                    Maybe.withDefault "" maybeChatId

                modelWithPending =
                    { baseModel
                        | pendingChatInit = Just chatId
                        , sessionInitialized = False
                    }
            in
            case model.connectionStatus of
                Connected socketId ->
                    if not model.sessionInitialized then
                        sendInitMessage socketId chatId modelWithPending
                    else
                        U2.pure modelWithPending

                _ ->
                    U2.pure modelWithPending

        Just Navigation.Agents ->
            -- Fetch agents when navigating to Agents page
            ( baseModel, Agents.fetchAgents AgentsMsg )

        Nothing ->
            U2.pure baseModel


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

                DoneMessage maybeChatId ->
                    let
                        ( newModel, cmds ) =
                            Chat.receiveStreamDone (chatProtocol model) maybeChatId model.chat

                        -- If we received a new chat_id for a chat that didn't have one, update the URL
                        urlUpdateCmd =
                            case ( model.chat.chatId, maybeChatId ) of
                                ( Nothing, Just chatId ) ->
                                    Navigation.pushUrl (Navigation.routeToString (Navigation.Chat (Just chatId)))

                                _ ->
                                    Cmd.none
                    in
                    ( newModel, Cmd.batch [ cmds, urlUpdateCmd ] )

                CancelledMessage ->
                    let
                        ( newModel, cmds ) =
                            Chat.receiveStreamCancelled (chatProtocol model) model.chat
                    in
                    ( newModel, Cmd.batch [ cmds, Ports.debugLog "Received cancelled message from server" ] )

                ReadyMessage maybeChatId ->
                    -- Session ready for existing chat - history already sent, scroll to bottom
                    ( model, Chat.scrollToBottom ChatMsg )

                HistoryMessage { role, content } ->
                    -- Add history message to chat (for reconnecting to existing chats)
                    Chat.receiveHistoryMessage (chatProtocol model) role content model.chat

                ErrorMessage errorMsg ->
                    Chat.receiveStreamError (chatProtocol model) errorMsg model.chat

                UiCommandMessage { command } ->
                    -- Handle UI commands from MCP tools
                    handleUiCommand command model

        Err _ ->
            U2.pure model


handleUiCommand : String -> Model -> ( Model, Cmd Msg )
handleUiCommand command model =
    case command of
        "open_sidebar" ->
            Chat.setSidebarVisible (chatProtocol model) True model.chat

        "close_sidebar" ->
            Chat.setSidebarVisible (chatProtocol model) False model.chat

        _ ->
            -- Unknown command, ignore
            U2.pure model


type ServerMessage
    = DeltaMessage String
    | DoneMessage (Maybe String)
    | CancelledMessage
    | ReadyMessage (Maybe String)
    | HistoryMessage { role : String, content : String }
    | ErrorMessage String
    | UiCommandMessage { command : String }


serverMessageDecoder : Decode.Decoder ServerMessage
serverMessageDecoder =
    let
        decodeByType msgType =
            case msgType of
                "delta" ->
                    Decode.map DeltaMessage (Decode.field "content" Decode.string)

                "done" ->
                    Decode.map DoneMessage
                        (Decode.maybe (Decode.field "chat_id" Decode.string))

                "cancelled" ->
                    Decode.succeed CancelledMessage

                "ready" ->
                    Decode.map ReadyMessage
                        (Decode.maybe (Decode.field "chat_id" Decode.string))

                "history" ->
                    Decode.map2 (\role content -> HistoryMessage { role = role, content = content })
                        (Decode.field "role" Decode.string)
                        (Decode.field "content" Decode.string)

                "error" ->
                    Decode.map ErrorMessage (Decode.field "message" Decode.string)

                "ui_command" ->
                    Decode.map (\cmd -> UiCommandMessage { command = cmd })
                        (Decode.field "command" Decode.string)

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
        , Browser.Events.onKeyDown (Decode.field "key" Decode.string |> Decode.map GlobalKeyDown)
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
        , viewSettingsModal model
        ]


viewPage : Model -> Html Msg
viewPage model =
    case model.route of
        Just Navigation.Intro ->
            Intro.view { toMsg = IntroMsg } model.intro

        Just (Navigation.Chat _) ->
            Chat.view
                { toMsg = ChatMsg
                , isConnected = isConnected model.connectionStatus
                , onReconnect = Reconnect
                , onGoHome = GoHome
                , onOpenSettings = OpenSettingsModal
                , submitShortcut = model.appSettings.submitShortcut
                }
                model.chat

        Just Navigation.Agents ->
            Agents.view
                { toMsg = AgentsMsg
                , isConnected = isConnected model.connectionStatus
                , onGoHome = GoHome
                , onOpenSettings = OpenSettingsModal
                , submitShortcut = model.appSettings.submitShortcut
                }
                model.agents

        Nothing ->
            Intro.view { toMsg = IntroMsg } model.intro


viewSettingsModal : Model -> Html Msg
viewSettingsModal model =
    if model.showSettingsModal then
        HS.div
            [ HA.class "modal-overlay"
            , HE.onClick CloseSettingsModal
            ]
            [ HS.div
                [ HA.class "modal-content"
                , HE.stopPropagationOn "click" (Decode.succeed ( NoOp, True ))
                ]
                [ HS.h3
                    [ HA.class "modal-title" ]
                    [ HS.text "Settings" ]
                , HS.div
                    [ HA.class "settings-form" ]
                    [ HS.label
                        [ HA.class "settings-label" ]
                        [ HS.text "Query Submit Shortcut" ]
                    , HS.div
                        [ HA.class "settings-radio-group" ]
                        (List.map (viewShortcutOption model.pendingSettings.submitShortcut) Settings.allSubmitShortcuts)
                    ]
                , HS.div
                    [ HA.class "modal-buttons" ]
                    [ HS.button
                        [ HA.class "modal-cancel-button"
                        , HE.onClick CloseSettingsModal
                        ]
                        [ HS.text "Cancel" ]
                    , HS.button
                        [ HA.class "modal-save-button"
                        , HE.onClick SaveSettings
                        ]
                        [ HS.text "Save" ]
                    ]
                ]
            ]

    else
        HS.text ""


viewShortcutOption : SubmitShortcut -> SubmitShortcut -> Html Msg
viewShortcutOption selectedShortcut shortcut =
    let
        isSelected =
            selectedShortcut == shortcut

        optionClass =
            if isSelected then
                "settings-radio-option settings-radio-option-selected"

            else
                "settings-radio-option"
    in
    HS.label
        [ HA.class optionClass
        , HE.onClick (SetPendingShortcut shortcut)
        ]
        [ HS.input
            [ HA.type_ "radio"
            , HA.class "settings-radio-input"
            , HA.name "submit-shortcut"
            , HA.checked isSelected
            ]
            []
        , HS.span
            [ HA.class "settings-radio-text" ]
            [ HS.text (Settings.submitShortcutLabel shortcut) ]
        ]


isConnected : ConnectionStatus -> Bool
isConnected status =
    case status of
        Connected _ ->
            True

        _ ->
            False
