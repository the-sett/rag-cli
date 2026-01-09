module Markdown.ChatMarkBlock exposing
    ( ChatMarkBlock(..)
    , HeadingInfo
    , StreamState
    , extractHeadings
    , feedDelta
    , finishStream
    , getPending
    , initStreamState
    , parseMarkdown
    , renderBlocks
    , renderBlocksWithCopy
    , renderBlocksWithIds
    , renderBlocksWithIdsAndCopy
    )

{-| High-level API for streaming markdown rendering.

This module combines the BlockSplitter, Assembler, and StyledRenderer
to provide a complete solution for rendering streaming markdown.

-}

import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Markdown.Block
import Markdown.Parser
import Markdown.RawBlock as RawBlock exposing (ListType(..), RawBlock(..))
import Markdown.Renderer
import Markdown.StyledRenderer
import Markdown.TextChunks as TextChunks


{-| A chat message block that can be complete, pending, or have an error.
-}
type ChatMarkBlock
    = CompleteBlock String Markdown.Block.Block
    | PendingBlock String
    | ErrorBlock String String


{-| Information about a heading extracted from markdown.
-}
type alias HeadingInfo =
    { level : Int
    , text : String
    }


{-| The state for streaming markdown.
-}
type alias StreamState =
    { splitterState : TextChunks.TextChunks
    , completedBlocks : List ChatMarkBlock
    }


{-| Initialize a new stream state.
-}
initStreamState : StreamState
initStreamState =
    { splitterState = TextChunks.init
    , completedBlocks = []
    }


{-| Parse a complete markdown string into ChatMarkBlocks.
    Used for loading history messages.
-}
parseMarkdown : String -> List ChatMarkBlock
parseMarkdown text =
    let
        -- Use the streaming infrastructure to parse - feed all text then finish
        state =
            feedDelta text initStreamState
    in
    finishStream state


{-| Feed a delta (chunk of text) into the stream.
-}
feedDelta : String -> StreamState -> StreamState
feedDelta delta state =
    let
        newSplitterState =
            TextChunks.feed delta state.splitterState

        -- Parse any newly completed blocks
        previousCompletedCount =
            List.length (TextChunks.getCompleted state.splitterState)

        allCompleted =
            TextChunks.getCompleted newSplitterState

        newlyCompleted =
            List.drop previousCompletedCount allCompleted

        -- Parse and add new completed blocks
        newParsedBlocks =
            List.concatMap parseRawBlock newlyCompleted
    in
    { splitterState = newSplitterState
    , completedBlocks = state.completedBlocks ++ newParsedBlocks
    }


{-| Finish the stream and finalize all blocks.
-}
finishStream : StreamState -> List ChatMarkBlock
finishStream state =
    let
        finalSplitterState =
            TextChunks.finish state.splitterState

        -- Get any remaining completed blocks
        previousCompletedCount =
            List.length (TextChunks.getCompleted state.splitterState)

        allCompleted =
            TextChunks.getCompleted finalSplitterState

        newlyCompleted =
            List.drop previousCompletedCount allCompleted

        newParsedBlocks =
            List.concatMap parseRawBlock newlyCompleted
    in
    state.completedBlocks ++ newParsedBlocks


{-| Get the current pending text (for display while streaming).
-}
getPending : StreamState -> String
getPending state =
    TextChunks.getPending state.splitterState


{-| Parse a raw block string into ChatMarkBlocks.
-}
parseRawBlock : String -> List ChatMarkBlock
parseRawBlock raw =
    let
        trimmed =
            String.trim raw
    in
    if String.isEmpty trimmed then
        []

    else
        case Markdown.Parser.parse raw of
            Ok blocks ->
                List.map (CompleteBlock raw) blocks

            Err deadEnds ->
                let
                    errorMsg =
                        deadEnds
                            |> List.map Markdown.Parser.deadEndToString
                            |> String.join "; "
                in
                [ ErrorBlock raw errorMsg ]


{-| Render a list of ChatMarkBlocks to Html.
-}
renderBlocks : List ChatMarkBlock -> String -> List (Html msg)
renderBlocks blocks pendingText =
    renderBlocksInternal Nothing blocks pendingText


{-| Render a list of ChatMarkBlocks to Html with a copy button on code blocks.
-}
renderBlocksWithCopy : (String -> msg) -> List ChatMarkBlock -> String -> List (Html msg)
renderBlocksWithCopy onCopy blocks pendingText =
    renderBlocksInternal (Just onCopy) blocks pendingText


renderBlocksInternal : Maybe (String -> msg) -> List ChatMarkBlock -> String -> List (Html msg)
renderBlocksInternal maybeCopyHandler blocks pendingText =
    let
        -- First, render completed blocks
        renderedCompleted =
            blocks
                |> List.map (renderChatMarkBlock maybeCopyHandler)

        -- Then render pending text if any
        renderedPending =
            if String.isEmpty (String.trim pendingText) then
                []

            else
                [ renderPending pendingText ]
    in
    renderedCompleted ++ renderedPending


{-| Render a single ChatMarkBlock.
-}
renderChatMarkBlock : Maybe (String -> msg) -> ChatMarkBlock -> Html msg
renderChatMarkBlock maybeCopyHandler block =
    case block of
        CompleteBlock _ parsedBlock ->
            renderMarkdownBlock maybeCopyHandler parsedBlock

        PendingBlock raw ->
            renderPending raw

        ErrorBlock _ errorMsg ->
            renderError errorMsg


{-| Render a parsed markdown block.
-}
renderMarkdownBlock : Maybe (String -> msg) -> Markdown.Block.Block -> Html msg
renderMarkdownBlock maybeCopyHandler block =
    let
        theRenderer =
            case maybeCopyHandler of
                Just onCopy ->
                    Markdown.StyledRenderer.rendererWithCopy onCopy

                Nothing ->
                    Markdown.StyledRenderer.renderer
    in
    case Markdown.Renderer.render theRenderer [ block ] of
        Ok [ rendered ] ->
            rendered

        Ok rendered ->
            HS.div [] rendered

        Err _ ->
            -- Fallback to showing raw (shouldn't happen with valid blocks)
            HS.pre [ HA.class "md-error" ] [ HS.text "Render error" ]


{-| Render pending/raw text.
-}
renderPending : String -> Html msg
renderPending raw =
    HS.pre [ HA.class "md-pending" ] [ HS.text raw ]


{-| Render error message.
-}
renderError : String -> Html msg
renderError errorMsg =
    HS.pre [ HA.class "md-error" ] [ HS.text errorMsg ]


{-| Extract heading information from a list of ChatMarkBlocks.
-}
extractHeadings : List ChatMarkBlock -> List HeadingInfo
extractHeadings blocks =
    blocks
        |> List.filterMap extractHeadingFromBlock


extractHeadingFromBlock : ChatMarkBlock -> Maybe HeadingInfo
extractHeadingFromBlock block =
    case block of
        CompleteBlock _ mdBlock ->
            case mdBlock of
                Markdown.Block.Heading level inlines ->
                    Just
                        { level = Markdown.Block.headingLevelToInt level
                        , text = Markdown.Block.extractInlineText inlines
                        }

                _ ->
                    Nothing

        _ ->
            Nothing


{-| Render a list of ChatMarkBlocks to Html, adding IDs to headings.
The idPrefix is used to generate unique IDs like "msg-0-heading-0".
-}
renderBlocksWithIds : String -> List ChatMarkBlock -> String -> List (Html msg)
renderBlocksWithIds idPrefix blocks pendingText =
    renderBlocksWithIdsInternal Nothing idPrefix blocks pendingText


{-| Render a list of ChatMarkBlocks to Html, adding IDs to headings and copy buttons on code blocks.
-}
renderBlocksWithIdsAndCopy : (String -> msg) -> String -> List ChatMarkBlock -> String -> List (Html msg)
renderBlocksWithIdsAndCopy onCopy idPrefix blocks pendingText =
    renderBlocksWithIdsInternal (Just onCopy) idPrefix blocks pendingText


renderBlocksWithIdsInternal : Maybe (String -> msg) -> String -> List ChatMarkBlock -> String -> List (Html msg)
renderBlocksWithIdsInternal maybeCopyHandler idPrefix blocks pendingText =
    let
        -- Render completed blocks with IDs
        ( renderedCompleted, _ ) =
            blocks
                |> List.foldl
                    (\block ( rendered, headingIdx ) ->
                        let
                            ( html, newIdx ) =
                                renderChatMarkBlockWithId maybeCopyHandler idPrefix headingIdx block
                        in
                        ( rendered ++ [ html ], newIdx )
                    )
                    ( [], 0 )

        -- Render pending text if any
        renderedPending =
            if String.isEmpty (String.trim pendingText) then
                []

            else
                [ renderPending pendingText ]
    in
    renderedCompleted ++ renderedPending


{-| Render a single ChatMarkBlock, adding ID to heading if applicable.
Returns the rendered Html and the next heading index.
-}
renderChatMarkBlockWithId : Maybe (String -> msg) -> String -> Int -> ChatMarkBlock -> ( Html msg, Int )
renderChatMarkBlockWithId maybeCopyHandler idPrefix headingIdx block =
    case block of
        CompleteBlock _ parsedBlock ->
            case parsedBlock of
                Markdown.Block.Heading _ _ ->
                    let
                        headingId =
                            idPrefix ++ "-heading-" ++ String.fromInt headingIdx
                    in
                    ( renderMarkdownBlockWithId maybeCopyHandler headingId parsedBlock, headingIdx + 1 )

                _ ->
                    ( renderMarkdownBlock maybeCopyHandler parsedBlock, headingIdx )

        PendingBlock raw ->
            ( renderPending raw, headingIdx )

        ErrorBlock _ errorMsg ->
            ( renderError errorMsg, headingIdx )


{-| Render a parsed markdown block with a specific ID.
-}
renderMarkdownBlockWithId : Maybe (String -> msg) -> String -> Markdown.Block.Block -> Html msg
renderMarkdownBlockWithId maybeCopyHandler headingId block =
    let
        theRenderer =
            case maybeCopyHandler of
                Just onCopy ->
                    Markdown.StyledRenderer.rendererWithCopy onCopy

                Nothing ->
                    Markdown.StyledRenderer.renderer
    in
    case Markdown.Renderer.render theRenderer [ block ] of
        Ok [ rendered ] ->
            -- Wrap the heading in a div with the ID
            HS.div [ HA.id headingId ] [ rendered ]

        Ok rendered ->
            HS.div [ HA.id headingId ] rendered

        Err _ ->
            HS.pre [ HA.class "md-error", HA.id headingId ] [ HS.text "Render error" ]
