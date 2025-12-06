module Main exposing (main)

import Browser
import Browser.Dom as Dom
import Css.Global
import Html
import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Html.Styled.Events as HE
import Json.Decode as Decode exposing (Value)
import Main.Debug
import Main.Style
import Markdown.ChatMarkBlock as ChatMarkBlock exposing (ChatMarkBlock, StreamState)
import Ports
import Procedure.Program
import Task
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
    , userInput : String
    , messages : List ChatMessage
    , streamState : StreamState
    , isWaitingForResponse : Bool
    , tocEntriesHistory : List TocEntry -- From finalized messages (cached)
    , tocEntriesStreaming : List TocEntry -- From current streaming response
    , inputFocused : Bool
    , lastEnterTime : Int
    , activeTocEntryId : Maybe String -- Currently visible TOC entry
    , tocElementPositions : List { id : String, top : Float } -- Cached element positions
    }


type alias ChatMessage =
    { role : String
    , blocks : List ChatMarkBlock
    }


{-| Table of contents entry derived from markdown headings or user queries.
-}
type alias TocEntry =
    { id : String -- Unique DOM id for scrolling target
    , level : Int -- Heading level (1-6), 0 for user queries
    , text : String -- Heading text content or truncated query
    , messageIndex : Int -- Which message this belongs to
    , isUserQuery : Bool -- True for user query entries
    }


{-| Scroll event data decoded from the messages container.
-}
type alias ScrollEvent =
    { scrollTop : Float
    , clientHeight : Float
    }


{-| Decoder for scroll events on the messages container.
-}
scrollEventDecoder : Decode.Decoder Msg
scrollEventDecoder =
    Decode.map2 ScrollEvent
        (Decode.at [ "target", "scrollTop" ] Decode.float)
        (Decode.at [ "target", "clientHeight" ] Decode.float)
        |> Decode.map OnScroll


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
    | ScrollToEntry String
    | ScrollResult (Result Dom.Error ())
    | InputFocused
    | InputBlurred
    | InputKeyDown Int
    | OnScroll ScrollEvent
    | GotElementPosition { id : String, top : Float }



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
            , streamState = ChatMarkBlock.initStreamState
            , isWaitingForResponse = False
            , tocEntriesHistory = []
            , tocEntriesStreaming = []
            , inputFocused = False
            , lastEnterTime = 0
            , activeTocEntryId = Nothing
            , tocElementPositions = []
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
            U2.pure model
                |> U2.andThen (handleWsOpened result)

        WsSent _ ->
            U2.pure model

        WsClosed _ ->
            U2.pure model
                |> U2.andThen setDisconnected

        WsMessage _ payload ->
            U2.pure model
                |> U2.andThen (handleServerMessage payload)

        WsClosedAsync _ ->
            U2.pure model
                |> U2.andThen setDisconnected

        WsError _ _ ->
            U2.pure model

        UserInputChanged input ->
            U2.pure { model | userInput = input }

        SendMessage ->
            U2.pure model
                |> U2.andThen sendUserMessage

        Reconnect ->
            U2.pure { model | connectionStatus = Connecting }
                |> U2.andThen reconnectWebSocket

        ScrollToEntry targetId ->
            U2.pure { model | activeTocEntryId = Just targetId }
                |> U2.andThen (scrollToTocEntry targetId)

        ScrollResult _ ->
            U2.pure model

        InputFocused ->
            U2.pure { model | inputFocused = True }

        InputBlurred ->
            U2.pure { model | inputFocused = False }

        InputKeyDown currentTime ->
            U2.pure model
                |> U2.andThen (handleInputKeyDown currentTime)

        OnScroll scrollEvent ->
            U2.pure model
                |> U2.andThen (updateActiveTocEntry scrollEvent)

        GotElementPosition position ->
            U2.pure model
                |> U2.andThen (cacheElementPosition position)



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


sendUserMessage : Model -> ( Model, Cmd Msg )
sendUserMessage model =
    case model.connectionStatus of
        Connected socketId ->
            if String.trim model.userInput /= "" then
                let
                    queryJson =
                        "{\"type\":\"query\",\"content\":" ++ encodeString model.userInput ++ "}"

                    newMessage =
                        { role = "user", blocks = [ ChatMarkBlock.PendingBlock model.userInput ] }

                    newMessageIndex =
                        List.length model.messages

                    newMessageId =
                        "msg-" ++ String.fromInt newMessageIndex

                    userTocEntry =
                        generateUserTocEntry newMessageIndex model.userInput

                    newTocEntriesHistory =
                        model.tocEntriesHistory ++ [ userTocEntry ]
                in
                ( { model
                    | userInput = ""
                    , messages = model.messages ++ [ newMessage ]
                    , isWaitingForResponse = True
                    , streamState = ChatMarkBlock.initStreamState
                    , tocEntriesHistory = newTocEntriesHistory
                  }
                , Cmd.batch
                    [ wsApi.send socketId queryJson WsSent
                    , scrollToEntry newMessageId
                    , queryTocElementPosition newMessageId
                    ]
                )

            else
                U2.pure model

        _ ->
            U2.pure model


scrollToTocEntry : String -> Model -> ( Model, Cmd Msg )
scrollToTocEntry targetId model =
    ( model, scrollToEntry targetId )


handleInputKeyDown : Int -> Model -> ( Model, Cmd Msg )
handleInputKeyDown currentTime model =
    let
        timeDiff =
            currentTime - model.lastEnterTime
    in
    if timeDiff < 400 && timeDiff > 0 then
        -- Double-enter detected, trigger send
        U2.pure { model | lastEnterTime = 0 }
            |> U2.andThen sendUserMessage

    else
        U2.pure { model | lastEnterTime = currentTime }


updateActiveTocEntry : ScrollEvent -> Model -> ( Model, Cmd Msg )
updateActiveTocEntry scrollEvent model =
    let
        -- The active entry is the one whose top is above the bottom of the
        -- top 1/3 of the container, and comes earliest among all such entries
        thresholdY =
            scrollEvent.scrollTop + (scrollEvent.clientHeight / 3)

        -- Use cached element positions
        relevantPositions =
            model.tocElementPositions
                |> List.sortBy .top

        -- Find entries whose top is above the threshold (visible in top 1/3)
        -- Take the last one that's above threshold (earliest in scroll order that's visible)
        activeId =
            relevantPositions
                |> List.filter (\pos -> pos.top <= thresholdY)
                |> List.reverse
                |> List.head
                |> Maybe.map .id
    in
    U2.pure { model | activeTocEntryId = activeId }


cacheElementPosition : { id : String, top : Float } -> Model -> ( Model, Cmd Msg )
cacheElementPosition position model =
    if String.isEmpty position.id then
        U2.pure model

    else
        let
            -- Remove any existing entry for this id, then add the new one
            filteredPositions =
                List.filter (\p -> p.id /= position.id) model.tocElementPositions
        in
        U2.pure { model | tocElementPositions = filteredPositions ++ [ position ] }


{-| Scroll to a heading in the messages container.

To position the heading at the top of the scrollable container, we need to
calculate where the element is within the container's scrollable content:

    targetScrollPosition =
        elementDocY - containerDocY + currentScrollTop - topPadding

Where:

  - elementDocY: element's Y position in the document
  - containerDocY: container's Y position in the document
  - currentScrollTop: how much the container is already scrolled (viewport.y)
  - topPadding: small offset so element sits nicely below the top edge

This gives the absolute position of the element within the container's content,
which is independent of the current scroll position.

-}
scrollToEntry : String -> Cmd Msg
scrollToEntry targetId =
    Dom.getElement targetId
        |> Task.andThen
            (\element ->
                Dom.getElement "messages-container"
                    |> Task.andThen
                        (\container ->
                            Dom.getViewportOf "messages-container"
                                |> Task.andThen
                                    (\viewport ->
                                        let
                                            -- Element's position within container's content
                                            -- Subtract 8px so it sits nicely below the top edge
                                            targetY =
                                                element.element.y
                                                    - container.element.y
                                                    + viewport.viewport.y
                                                    - 8
                                        in
                                        Dom.setViewportOf "messages-container" 0 (max 0 targetY)
                                    )
                        )
            )
        |> Task.attempt ScrollResult


{-| Query position of a single TOC element and add it to the cached positions.
Position is calculated relative to the scroll container's content.
-}
queryTocElementPosition : String -> Cmd Msg
queryTocElementPosition elementId =
    Dom.getElement "messages-container"
        |> Task.andThen
            (\container ->
                Dom.getViewportOf "messages-container"
                    |> Task.andThen
                        (\viewport ->
                            Dom.getElement elementId
                                |> Task.map
                                    (\el ->
                                        let
                                            -- Position within scrollable content
                                            relativeTop =
                                                el.element.y
                                                    - container.element.y
                                                    + viewport.viewport.y
                                        in
                                        { id = elementId, top = relativeTop }
                                    )
                        )
            )
        |> Task.attempt
            (\result ->
                case result of
                    Ok position ->
                        GotElementPosition position

                    Err _ ->
                        -- Element not found, ignore
                        GotElementPosition { id = "", top = 0 }
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
                |> String.replace "\u{000D}" "\\r"
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
                    U2.pure model
                        |> U2.andThen (processStreamingDelta content)
                        |> U2.andThen queryNewTocEntryPositions

                DoneMessage ->
                    U2.pure model
                        |> U2.andThen finalizeStreamingResponse
                        |> U2.andThen queryNewTocEntryPositions

                ErrorMessage errorMsg ->
                    U2.pure model
                        |> U2.andThen (addErrorMessage errorMsg)

        Err _ ->
            U2.pure model


processStreamingDelta : String -> Model -> ( Model, Cmd Msg )
processStreamingDelta content model =
    let
        newStreamState =
            ChatMarkBlock.feedDelta content model.streamState

        streamingMessageIndex =
            List.length model.messages

        newStreamingTocEntries =
            generateTocEntriesForBlocks streamingMessageIndex newStreamState.completedBlocks
    in
    U2.pure
        { model
            | streamState = newStreamState
            , tocEntriesStreaming = newStreamingTocEntries
        }


finalizeStreamingResponse : Model -> ( Model, Cmd Msg )
finalizeStreamingResponse model =
    let
        finalBlocks =
            ChatMarkBlock.finishStream model.streamState

        assistantMessage =
            { role = "assistant", blocks = finalBlocks }

        messageIndex =
            List.length model.messages

        finalTocEntries =
            generateTocEntriesForBlocks messageIndex finalBlocks

        newTocEntriesHistory =
            model.tocEntriesHistory ++ finalTocEntries
    in
    U2.pure
        { model
            | messages = model.messages ++ [ assistantMessage ]
            , streamState = ChatMarkBlock.initStreamState
            , isWaitingForResponse = False
            , tocEntriesHistory = newTocEntriesHistory
            , tocEntriesStreaming = []
        }


addErrorMessage : String -> Model -> ( Model, Cmd Msg )
addErrorMessage errorMsg model =
    let
        errorChatMessage =
            { role = "error", blocks = [ ChatMarkBlock.ErrorBlock "" errorMsg ] }
    in
    U2.pure
        { model
            | messages = model.messages ++ [ errorChatMessage ]
            , streamState = ChatMarkBlock.initStreamState
            , isWaitingForResponse = False
            , tocEntriesStreaming = []
        }


queryNewTocEntryPositions : Model -> ( Model, Cmd Msg )
queryNewTocEntryPositions model =
    let
        -- Get IDs of entries we already have positions for
        cachedIds =
            List.map .id model.tocElementPositions

        -- Find all current TOC entries (history + streaming)
        allCurrentEntries =
            model.tocEntriesHistory ++ model.tocEntriesStreaming

        -- Query positions for entries we don't have cached yet
        newEntries =
            List.filter (\e -> not (List.member e.id cachedIds)) allCurrentEntries

        positionQueries =
            List.map (\e -> queryTocElementPosition e.id) newEntries
    in
    ( model, Cmd.batch positionQueries )


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


{-| Generate TOC entries from a list of blocks at a given message index.
Used for both streaming blocks and finalized message blocks.
-}
generateTocEntriesForBlocks : Int -> List ChatMarkBlock -> List TocEntry
generateTocEntriesForBlocks messageIndex blocks =
    let
        headings =
            ChatMarkBlock.extractHeadings blocks

        idPrefix =
            "msg-" ++ String.fromInt messageIndex
    in
    headings
        |> List.indexedMap
            (\headingIdx heading ->
                { id = idPrefix ++ "-heading-" ++ String.fromInt headingIdx
                , level = heading.level
                , text = heading.text
                , messageIndex = messageIndex
                , isUserQuery = False
                }
            )


{-| Generate a TOC entry for a user query message.
Truncates to first 50 characters.
-}
generateUserTocEntry : Int -> String -> TocEntry
generateUserTocEntry messageIndex queryText =
    let
        -- Get first line and truncate to 50 chars
        firstLine =
            queryText
                |> String.lines
                |> List.head
                |> Maybe.withDefault queryText

        truncatedText =
            if String.length firstLine > 50 then
                String.left 50 firstLine

            else
                firstLine
    in
    { id = "msg-" ++ String.fromInt messageIndex
    , level = 0
    , text = truncatedText
    , messageIndex = messageIndex
    , isUserQuery = True
    }



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
        [ -- Main.Debug.global |> Css.Global.global
          Main.Style.style |> Css.Global.global
        , HS.div
            [ HA.class "main-layout" ]
            [ viewSidebar model
            , HS.div
                [ HA.class "content-column" ]
                [ HS.div
                    [ HA.class "messages-wrapper" ]
                    [ viewMessages model
                    , HS.div [ HA.class "messages-fade" ] []
                    ]
                , viewInput model
                ]
            ]
        ]


{-| Render the left sidebar with connection status and table of contents.
-}
viewSidebar : Model -> Html Msg
viewSidebar model =
    let
        allTocEntries =
            model.tocEntriesHistory ++ model.tocEntriesStreaming

        -- Pair each entry with (prevWasUserQuery, isActive)
        entriesWithContext =
            List.map2
                (\prevWasUser entry ->
                    { prevWasUserQuery = prevWasUser
                    , entry = entry
                    , isActive = model.activeTocEntryId == Just entry.id
                    }
                )
                (False :: List.map .isUserQuery allTocEntries)
                allTocEntries
    in
    HS.nav
        [ HA.class "toc-sidebar" ]
        [ viewConnectionStatus model.connectionStatus
        , HS.h2
            [ HA.class "toc-title" ]
            [ HS.text "Contents" ]
        , if List.isEmpty allTocEntries then
            HS.p
                [ HA.class "toc-empty" ]
                [ HS.text "No headings yet" ]

          else
            HS.ul
                [ HA.class "toc-list" ]
                (List.map viewTocEntryWithContext entriesWithContext)
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


{-| Context for rendering a TOC entry.
-}
type alias TocEntryContext =
    { prevWasUserQuery : Bool
    , entry : TocEntry
    , isActive : Bool
    }


{-| Render a single TOC entry with context about the previous entry.
-}
viewTocEntryWithContext : TocEntryContext -> Html Msg
viewTocEntryWithContext context =
    let
        entry =
            context.entry

        entryClass =
            if entry.isUserQuery then
                "toc-entry-user"

            else
                "toc-level-" ++ String.fromInt entry.level

        -- Add adjacent class if this user entry follows another user entry
        adjacentClass =
            if entry.isUserQuery && context.prevWasUserQuery then
                [ HA.class "toc-entry-user-adjacent" ]

            else
                []

        -- Add active class if this is the currently visible entry
        activeClass =
            if context.isActive then
                [ HA.class "toc-entry-active" ]

            else
                []
    in
    HS.li
        ([ HA.class "toc-entry"
         , HA.class entryClass
         ]
            ++ adjacentClass
            ++ activeClass
        )
        [ HS.button
            [ HA.class "toc-link"
            , HE.onClick (ScrollToEntry entry.id)
            ]
            [ HS.text entry.text ]
        ]


viewMessages : Model -> Html Msg
viewMessages model =
    let
        -- Get pending text from stream state for in-progress rendering
        pendingText =
            ChatMarkBlock.getPending model.streamState

        -- Add streaming message if there's activity
        streamingMessage =
            if model.isWaitingForResponse then
                [ viewStreamingMessage (List.length model.messages) model.streamState pendingText ]

            else
                []

        allContent =
            List.indexedMap viewMessageWithIndex model.messages ++ streamingMessage

        -- Dynamic spacer that fills remaining viewport space
        -- This allows the last message to be scrolled to the top
        spacer =
            HS.div [ HA.class "messages-spacer" ] []
    in
    HS.div
        [ HA.class "messages-container"
        , HA.id "messages-container"
        , HE.on "scroll" scrollEventDecoder
        ]
        [ HS.div
            [ HA.class "messages-content" ]
            (if List.isEmpty model.messages && not model.isWaitingForResponse then
                [ HS.p
                    [ HA.class "messages-empty" ]
                    [ HS.text "No messages yet. Send a message to start chatting." ]
                ]

             else
                allContent ++ [ spacer ]
            )
        ]


viewStreamingMessage : Int -> StreamState -> String -> Html Msg
viewStreamingMessage msgIndex streamState pendingText =
    let
        completedBlocks =
            streamState.completedBlocks

        idPrefix =
            "msg-" ++ String.fromInt msgIndex
    in
    HS.div
        [ HA.class "message"
        , HA.class "message-assistant"
        ]
        [ HS.div
            [ HA.class "message-content" ]
            (ChatMarkBlock.renderBlocksWithIds idPrefix completedBlocks pendingText)
        ]


{-| Render a message with its index for ID generation.
-}
viewMessageWithIndex : Int -> ChatMessage -> Html Msg
viewMessageWithIndex msgIndex message =
    let
        roleClass =
            case message.role of
                "user" ->
                    "message-user"

                "assistant" ->
                    "message-assistant"

                "error" ->
                    "message-error"

                _ ->
                    "message-assistant"

        idPrefix =
            "msg-" ++ String.fromInt msgIndex

        -- Use renderBlocksWithIds for assistant messages to enable TOC navigation
        renderedContent =
            if message.role == "assistant" then
                ChatMarkBlock.renderBlocksWithIds idPrefix message.blocks ""

            else
                ChatMarkBlock.renderBlocks message.blocks ""
    in
    HS.div
        [ HA.class "message"
        , HA.class roleClass
        , HA.id idPrefix
        ]
        [ HS.div
            [ HA.class "message-content" ]
            renderedContent
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

        -- Determine wrapper state class for border colors
        wrapperStateClass =
            if isDisabled then
                "input-wrapper-inactive"

            else if model.inputFocused then
                "input-wrapper-focused"

            else
                "input-wrapper-ready"

        canSend =
            not isDisabled && String.trim model.userInput /= ""
    in
    HS.div
        [ HA.class "input-container" ]
        [ HS.div
            [ HA.class "input-wrapper"
            , HA.class wrapperStateClass
            ]
            [ HS.textarea
                [ HA.class "input-textarea"
                , HA.value model.userInput
                , HE.onInput UserInputChanged
                , HE.onFocus InputFocused
                , HE.onBlur InputBlurred
                , HE.preventDefaultOn "keydown" (enterKeyDecoder model.lastEnterTime)
                , HA.placeholder "Type your message... (press Enter twice to send)"
                , HA.disabled isDisabled
                ]
                []
            , HS.div
                [ HA.class "input-toolbar" ]
                [ HS.button
                    [ HA.class "input-send-button"
                    , if canSend then
                        HA.class "input-send-button-enabled"

                      else
                        HA.class "input-send-button-disabled"
                    , HE.onClick SendMessage
                    , HA.disabled (not canSend)
                    , HA.title "Send message"
                    ]
                    [ HS.text "→" ]
                ]
            ]
        ]


{-| Decoder for Enter key press that captures the timestamp.
Only triggers on Enter key, ignores other keys.
Returns (Msg, preventDefault) - we prevent default on double-enter to stop
the newline from being added after the input is cleared.
-}
enterKeyDecoder : Int -> Decode.Decoder ( Msg, Bool )
enterKeyDecoder lastEnterTime =
    Decode.field "key" Decode.string
        |> Decode.andThen
            (\key ->
                if key == "Enter" then
                    Decode.field "timeStamp" Decode.float
                        |> Decode.map
                            (\timestamp ->
                                let
                                    currentTime =
                                        round timestamp

                                    timeDiff =
                                        currentTime - lastEnterTime

                                    isDoubleEnter =
                                        timeDiff < 400 && timeDiff > 0
                                in
                                ( InputKeyDown currentTime, isDoubleEnter )
                            )

                else
                    Decode.fail "Not Enter key"
            )
