module Pages.Chat.Update exposing
    ( Protocol
    , update
    , receiveStreamDelta
    , receiveStreamDone
    , receiveStreamCancelled
    , receiveStreamError
    , receiveHistoryMessage
    , scrollToBottom
    )

{-| Update logic for the Chat page.
-}

import Browser.Dom as Dom
import File
import Markdown.ChatMarkBlock as ChatMarkBlock
import Pages.Chat.Model exposing (ChatMessage, Model, ScrollEvent, TocEntry)
import Pages.Chat.Msg exposing (Msg(..))
import Process
import Task
import Update2 as U2


{-| Protocol for communicating with the parent.
-}
type alias Protocol model msg =
    { toMsg : Msg -> msg
    , onUpdate : ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onSendMessage : String -> ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onCancelStream : ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onCopy : String -> Cmd msg
    }


{-| Update the Chat page model.
-}
update : Protocol model msg -> Msg -> Model -> ( model, Cmd msg )
update protocol msg model =
    case msg of
        UserInputChanged input ->
            ( { model | userInput = input }, Cmd.none )
                |> protocol.onUpdate

        SendMessage ->
            sendUserMessage protocol model

        CancelStream ->
            cancelStream protocol model

        CopyToClipboard text ->
            ( model, protocol.onCopy text )
                |> protocol.onUpdate

        ScrollToEntry targetId ->
            ( { model | activeTocEntryId = Just targetId }
            , scrollToEntry targetId |> Cmd.map protocol.toMsg
            )
                |> protocol.onUpdate

        ScrollResult _ ->
            ( model, Cmd.none )
                |> protocol.onUpdate

        InputFocused ->
            ( { model | inputFocused = True }, Cmd.none )
                |> protocol.onUpdate

        InputBlurred ->
            ( { model | inputFocused = False }, Cmd.none )
                |> protocol.onUpdate

        InputKeyDown currentTime ->
            handleInputKeyDown protocol currentTime model

        OnScroll scrollEvent ->
            updateActiveTocEntry protocol scrollEvent model

        GotElementPosition position ->
            cacheElementPosition protocol position model

        RefreshTocPositions ->
            refreshAllTocPositions protocol model

        StreamDelta content ->
            processStreamingDelta protocol content model

        StreamDone maybeChatId ->
            finalizeStreamingResponse protocol maybeChatId model

        StreamCancelled ->
            handleStreamCancelled protocol model

        StreamError errorMsg ->
            addErrorMessage protocol errorMsg model

        DragEnter ->
            ( { model | isDraggingOver = True }, Cmd.none )
                |> protocol.onUpdate

        DragLeave ->
            ( { model | isDraggingOver = False }, Cmd.none )
                |> protocol.onUpdate

        FilesDropped file _ ->
            -- Read the first file's content as text
            ( { model | isDraggingOver = False }
            , File.toString file
                |> Task.perform GotFileContent
                |> Cmd.map protocol.toMsg
            )
                |> protocol.onUpdate

        GotFileContent content ->
            -- Append the file content to the user input
            let
                newInput =
                    if String.isEmpty model.userInput then
                        content

                    else
                        model.userInput ++ "\n" ++ content
            in
            ( { model | userInput = newInput }, Cmd.none )
                |> protocol.onUpdate


{-| Handle incoming stream delta from websocket (called from Main).
-}
receiveStreamDelta : Protocol model msg -> String -> Model -> ( model, Cmd msg )
receiveStreamDelta protocol content model =
    processStreamingDelta protocol content model


{-| Handle stream completion from websocket (called from Main).
-}
receiveStreamDone : Protocol model msg -> Maybe String -> Model -> ( model, Cmd msg )
receiveStreamDone protocol maybeChatId model =
    finalizeStreamingResponse protocol maybeChatId model


{-| Handle stream error from websocket (called from Main).
-}
receiveStreamError : Protocol model msg -> String -> Model -> ( model, Cmd msg )
receiveStreamError protocol errorMsg model =
    addErrorMessage protocol errorMsg model


{-| Handle stream cancelled from websocket (called from Main).
-}
receiveStreamCancelled : Protocol model msg -> Model -> ( model, Cmd msg )
receiveStreamCancelled protocol model =
    handleStreamCancelled protocol model


{-| Handle history message from websocket (called from Main).
    Adds a complete message from chat history when reconnecting.
-}
receiveHistoryMessage : Protocol model msg -> String -> String -> Model -> ( model, Cmd msg )
receiveHistoryMessage protocol role content model =
    addHistoryMessage protocol role content model



-- Internal helpers


sendUserMessage : Protocol model msg -> Model -> ( model, Cmd msg )
sendUserMessage protocol model =
    if String.trim model.userInput /= "" then
        let
            newMessage =
                { role = "user"
                , blocks = [ ChatMarkBlock.PendingBlock model.userInput ]
                }

            newMessageIndex =
                List.length model.messages

            newMessageId =
                "msg-" ++ String.fromInt newMessageIndex

            userTocEntry =
                generateUserTocEntry newMessageIndex model.userInput

            newTocEntriesHistory =
                model.tocEntriesHistory ++ [ userTocEntry ]

            messageContent =
                model.userInput

            newModel =
                { model
                    | userInput = ""
                    , pendingUserInput = Just model.userInput  -- Save for potential cancel
                    , messages = model.messages ++ [ newMessage ]
                    , isWaitingForResponse = True
                    , streamState = ChatMarkBlock.initStreamState
                    , tocEntriesHistory = newTocEntriesHistory
                }

            cmds =
                Cmd.batch
                    [ scrollToEntry newMessageId |> Cmd.map protocol.toMsg
                    , queryTocElementPosition newMessageId |> Cmd.map protocol.toMsg
                    ]
        in
        ( newModel, cmds )
            |> protocol.onSendMessage messageContent

    else
        ( model, Cmd.none )
            |> protocol.onUpdate


cancelStream : Protocol model msg -> Model -> ( model, Cmd msg )
cancelStream protocol model =
    if model.isWaitingForResponse then
        -- Immediately reset to editing state - don't wait for server confirmation
        -- Restore the original prompt, remove the user message, and focus input
        let
            restoredInput =
                Maybe.withDefault "" model.pendingUserInput

            -- Remove the last user message (the one we're cancelling)
            newMessages =
                List.take (List.length model.messages - 1) model.messages

            -- Remove the last TOC entry (the user query we're cancelling)
            newTocHistory =
                List.take (List.length model.tocEntriesHistory - 1) model.tocEntriesHistory

            newModel =
                { model
                    | userInput = restoredInput
                    , pendingUserInput = Nothing
                    , messages = newMessages
                    , tocEntriesHistory = newTocHistory
                    , isWaitingForResponse = False
                    , streamState = ChatMarkBlock.initStreamState
                    , tocEntriesStreaming = []
                }

            focusCmd =
                Dom.focus "chat-input"
                    |> Task.attempt (\_ -> InputFocused)
                    |> Cmd.map protocol.toMsg
        in
        ( newModel, focusCmd )
            |> protocol.onCancelStream

    else
        ( model, Cmd.none )
            |> protocol.onUpdate


handleStreamCancelled : Protocol model msg -> Model -> ( model, Cmd msg )
handleStreamCancelled protocol model =
    -- Keep any partial response that has been streamed so far
    let
        partialBlocks =
            model.streamState.completedBlocks

        pendingText =
            ChatMarkBlock.getPending model.streamState

        -- If we have some content, add it as a partial message
        ( newMessages, newTocHistory ) =
            if not (List.isEmpty partialBlocks) || not (String.isEmpty pendingText) then
                let
                    -- Add any pending text as a final block
                    finalBlocks =
                        if String.isEmpty pendingText then
                            partialBlocks

                        else
                            partialBlocks ++ [ ChatMarkBlock.PendingBlock pendingText ]

                    partialMessage =
                        { role = "assistant", blocks = finalBlocks }

                    messageIndex =
                        List.length model.messages

                    tocEntries =
                        generateTocEntriesForBlocks messageIndex finalBlocks
                in
                ( model.messages ++ [ partialMessage ]
                , model.tocEntriesHistory ++ tocEntries
                )

            else
                ( model.messages, model.tocEntriesHistory )
    in
    ( { model
        | isWaitingForResponse = False
        , streamState = ChatMarkBlock.initStreamState
        , tocEntriesStreaming = []
        , messages = newMessages
        , tocEntriesHistory = newTocHistory
      }
    , Cmd.none
    )
        |> protocol.onUpdate


handleInputKeyDown : Protocol model msg -> Int -> Model -> ( model, Cmd msg )
handleInputKeyDown protocol currentTime model =
    let
        timeDiff =
            currentTime - model.lastEnterTime
    in
    if timeDiff < 400 && timeDiff > 0 then
        sendUserMessage protocol { model | lastEnterTime = 0 }

    else
        ( { model | lastEnterTime = currentTime }, Cmd.none )
            |> protocol.onUpdate


updateActiveTocEntry : Protocol model msg -> ScrollEvent -> Model -> ( model, Cmd msg )
updateActiveTocEntry protocol scrollEvent model =
    let
        thresholdY =
            scrollEvent.scrollTop + (scrollEvent.clientHeight / 3)

        isAboveThreshold pos =
            pos.top <= thresholdY

        relevantPositions =
            model.tocElementPositions
                |> List.sortBy .top

        activeId =
            relevantPositions
                |> List.filter isAboveThreshold
                |> List.reverse
                |> List.head
                |> Maybe.map .id
    in
    ( { model | activeTocEntryId = activeId }, Cmd.none )
        |> protocol.onUpdate


cacheElementPosition : Protocol model msg -> { id : String, top : Float } -> Model -> ( model, Cmd msg )
cacheElementPosition protocol position model =
    if String.isEmpty position.id then
        ( model, Cmd.none )
            |> protocol.onUpdate

    else
        let
            isDifferentId p =
                p.id /= position.id

            filteredPositions =
                List.filter isDifferentId model.tocElementPositions
        in
        ( { model | tocElementPositions = filteredPositions ++ [ position ] }, Cmd.none )
            |> protocol.onUpdate


processStreamingDelta : Protocol model msg -> String -> Model -> ( model, Cmd msg )
processStreamingDelta protocol content model =
    -- Ignore deltas if we're not waiting for a response (e.g., after cancel)
    if not model.isWaitingForResponse then
        ( model, Cmd.none )
            |> protocol.onUpdate

    else
        let
            newStreamState =
                ChatMarkBlock.feedDelta content model.streamState

            streamingMessageIndex =
                List.length model.messages

            newStreamingTocEntries =
                generateTocEntriesForBlocks streamingMessageIndex newStreamState.completedBlocks

            newModel =
                { model
                    | streamState = newStreamState
                    , tocEntriesStreaming = newStreamingTocEntries
                }
        in
        queryNewTocEntryPositions protocol newModel


finalizeStreamingResponse : Protocol model msg -> Maybe String -> Model -> ( model, Cmd msg )
finalizeStreamingResponse protocol maybeChatId model =
    -- Ignore done messages if we're not waiting for a response (e.g., after cancel)
    if not model.isWaitingForResponse then
        ( model, Cmd.none )
            |> protocol.onUpdate

    else
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

            -- Update chat ID if provided by server and we don't have one yet
            newChatId =
                case ( model.chatId, maybeChatId ) of
                    ( Nothing, Just id ) ->
                        Just id

                    ( existing, _ ) ->
                        existing

            newModel =
                { model
                    | messages = model.messages ++ [ assistantMessage ]
                    , streamState = ChatMarkBlock.initStreamState
                    , isWaitingForResponse = False
                    , pendingUserInput = Nothing  -- Clear saved input on successful completion
                    , tocEntriesHistory = newTocEntriesHistory
                    , tocEntriesStreaming = []
                    , chatId = newChatId
                }
        in
        queryNewTocEntryPositions protocol newModel


addErrorMessage : Protocol model msg -> String -> Model -> ( model, Cmd msg )
addErrorMessage protocol errorMsg model =
    let
        errorChatMessage =
            { role = "error"
            , blocks = [ ChatMarkBlock.ErrorBlock "" errorMsg ]
            }
    in
    ( { model
        | messages = model.messages ++ [ errorChatMessage ]
        , streamState = ChatMarkBlock.initStreamState
        , isWaitingForResponse = False
        , tocEntriesStreaming = []
      }
    , Cmd.none
    )
        |> protocol.onUpdate


addHistoryMessage : Protocol model msg -> String -> String -> Model -> ( model, Cmd msg )
addHistoryMessage protocol role content model =
    let
        messageIndex =
            List.length model.messages

        -- User messages are kept as plain text (PendingBlock), assistant messages are parsed
        blocks =
            if role == "user" then
                [ ChatMarkBlock.PendingBlock content ]

            else
                ChatMarkBlock.parseMarkdown content

        historyMessage =
            { role = role, blocks = blocks }

        -- Generate TOC entries for this message
        tocEntries =
            if role == "user" then
                [ generateUserTocEntry messageIndex content ]

            else
                generateTocEntriesForBlocks messageIndex blocks

        newTocEntriesHistory =
            model.tocEntriesHistory ++ tocEntries

        newModel =
            { model
                | messages = model.messages ++ [ historyMessage ]
                , tocEntriesHistory = newTocEntriesHistory
            }
    in
    queryNewTocEntryPositions protocol newModel


queryNewTocEntryPositions : Protocol model msg -> Model -> ( model, Cmd msg )
queryNewTocEntryPositions protocol model =
    let
        cachedIds =
            List.map .id model.tocElementPositions

        allCurrentEntries =
            model.tocEntriesHistory ++ model.tocEntriesStreaming

        isNotCached entry =
            not (List.member entry.id cachedIds)

        newEntries =
            List.filter isNotCached allCurrentEntries

        positionQueries =
            List.map (.id >> queryTocElementPosition >> Cmd.map protocol.toMsg) newEntries
    in
    ( model, Cmd.batch positionQueries )
        |> protocol.onUpdate


refreshAllTocPositions : Protocol model msg -> Model -> ( model, Cmd msg )
refreshAllTocPositions protocol model =
    let
        -- Clear all cached positions and re-query all TOC entries
        allCurrentEntries =
            model.tocEntriesHistory ++ model.tocEntriesStreaming

        positionQueries =
            List.map (.id >> queryTocElementPosition >> Cmd.map protocol.toMsg) allCurrentEntries

        clearedModel =
            { model | tocElementPositions = [] }
    in
    ( clearedModel, Cmd.batch positionQueries )
        |> protocol.onUpdate


generateTocEntriesForBlocks : Int -> List ChatMarkBlock.ChatMarkBlock -> List TocEntry
generateTocEntriesForBlocks messageIndex blocks =
    let
        headings =
            ChatMarkBlock.extractHeadings blocks

        idPrefix =
            "msg-" ++ String.fromInt messageIndex

        toTocEntry headingIdx heading =
            { id = idPrefix ++ "-heading-" ++ String.fromInt headingIdx
            , level = heading.level
            , text = heading.text
            , messageIndex = messageIndex
            , isUserQuery = False
            }
    in
    headings
        |> List.indexedMap toTocEntry


generateUserTocEntry : Int -> String -> TocEntry
generateUserTocEntry messageIndex queryText =
    let
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


scrollToEntry : String -> Cmd Msg
scrollToEntry targetId =
    let
        addContainer element container =
            { element = element, container = container }

        getContainer element =
            Dom.getElement "messages-container"
                |> Task.map (addContainer element)

        addViewport ctx viewport =
            { element = ctx.element, container = ctx.container, viewport = viewport }

        getViewport ctx =
            Dom.getViewportOf "messages-container"
                |> Task.map (addViewport ctx)

        setScrollPosition { element, container, viewport } =
            let
                targetY =
                    element.element.y
                        - container.element.y
                        + viewport.viewport.y
                        - 8
            in
            Dom.setViewportOf "messages-container" 0 (max 0 targetY)
    in
    Dom.getElement targetId
        |> Task.andThen getContainer
        |> Task.andThen getViewport
        |> Task.andThen setScrollPosition
        |> Task.attempt ScrollResult


{-| Scroll the messages container to the bottom.
    Used when loading chat history.
-}
scrollToBottom : Cmd Msg
scrollToBottom =
    Cmd.batch
        [ Dom.getViewportOf "messages-container"
            |> Task.andThen
                (\viewport ->
                    Dom.setViewportOf "messages-container" 0 viewport.scene.height
                )
            |> Task.attempt ScrollResult
        , -- Delay the refresh to allow scroll to complete and DOM to settle
          Process.sleep 100
            |> Task.perform (\_ -> RefreshTocPositions)
        ]


queryTocElementPosition : String -> Cmd Msg
queryTocElementPosition elementId =
    let
        addViewport container viewport =
            { container = container, viewport = viewport }

        getViewport container =
            Dom.getViewportOf "messages-container"
                |> Task.map (addViewport container)

        addElement ctx el =
            { container = ctx.container, viewport = ctx.viewport, el = el }

        getElement ctx =
            Dom.getElement elementId
                |> Task.map (addElement ctx)

        calculatePosition { container, viewport, el } =
            let
                relativeTop =
                    el.element.y
                        - container.element.y
                        + viewport.viewport.y
            in
            { id = elementId, top = relativeTop }

        toMsg result =
            case result of
                Ok position ->
                    GotElementPosition position

                Err _ ->
                    GotElementPosition { id = "", top = 0 }
    in
    Dom.getElement "messages-container"
        |> Task.andThen getViewport
        |> Task.andThen getElement
        |> Task.map calculatePosition
        |> Task.attempt toMsg
