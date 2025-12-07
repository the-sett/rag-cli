module Pages.Chat.Update exposing
    ( Protocol
    , update
    , receiveStreamDelta
    , receiveStreamDone
    , receiveStreamError
    )

{-| Update logic for the Chat page.
-}

import Browser.Dom as Dom
import Markdown.ChatMarkBlock as ChatMarkBlock
import Pages.Chat.Model exposing (ChatMessage, Model, ScrollEvent, TocEntry)
import Pages.Chat.Msg exposing (Msg(..))
import Task
import Update2 as U2


{-| Protocol for communicating with the parent.
-}
type alias Protocol model msg =
    { toMsg : Msg -> msg
    , onUpdate : ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onSendMessage : String -> ( Model, Cmd msg ) -> ( model, Cmd msg )
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

        StreamDelta content ->
            processStreamingDelta protocol content model

        StreamDone ->
            finalizeStreamingResponse protocol model

        StreamError errorMsg ->
            addErrorMessage protocol errorMsg model


{-| Handle incoming stream delta from websocket (called from Main).
-}
receiveStreamDelta : Protocol model msg -> String -> Model -> ( model, Cmd msg )
receiveStreamDelta protocol content model =
    processStreamingDelta protocol content model


{-| Handle stream completion from websocket (called from Main).
-}
receiveStreamDone : Protocol model msg -> Model -> ( model, Cmd msg )
receiveStreamDone protocol model =
    finalizeStreamingResponse protocol model


{-| Handle stream error from websocket (called from Main).
-}
receiveStreamError : Protocol model msg -> String -> Model -> ( model, Cmd msg )
receiveStreamError protocol errorMsg model =
    addErrorMessage protocol errorMsg model



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


finalizeStreamingResponse : Protocol model msg -> Model -> ( model, Cmd msg )
finalizeStreamingResponse protocol model =
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

        newModel =
            { model
                | messages = model.messages ++ [ assistantMessage ]
                , streamState = ChatMarkBlock.initStreamState
                , isWaitingForResponse = False
                , tocEntriesHistory = newTocEntriesHistory
                , tocEntriesStreaming = []
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
