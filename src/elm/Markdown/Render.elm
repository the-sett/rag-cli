module Markdown.Render exposing
    ( ChatMarkBlock(..)
    , StreamState
    , initStreamState
    , feedDelta
    , finishStream
    , getPending
    , renderBlocks
    )

{-| High-level API for streaming markdown rendering.

This module combines the BlockSplitter, Assembler, and StyledRenderer
to provide a complete solution for rendering streaming markdown.

-}

import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Markdown.Assembler as Assembler exposing (AssembledBlock(..), ListType(..))
import Markdown.Block
import Markdown.BlockSplitter as Splitter
import Markdown.Parser
import Markdown.Renderer
import Markdown.StyledRenderer


{-| A chat message block that can be complete, pending, or have an error.
-}
type ChatMarkBlock
    = CompleteBlock String Markdown.Block.Block
    | PendingBlock String
    | ErrorBlock String String


{-| The state for streaming markdown.
-}
type alias StreamState =
    { splitterState : Splitter.State
    , completedBlocks : List ChatMarkBlock
    }


{-| Initialize a new stream state.
-}
initStreamState : StreamState
initStreamState =
    { splitterState = Splitter.init
    , completedBlocks = []
    }


{-| Feed a delta (chunk of text) into the stream.
-}
feedDelta : String -> StreamState -> StreamState
feedDelta delta state =
    let
        newSplitterState =
            Splitter.feed delta state.splitterState

        -- Parse any newly completed blocks
        previousCompletedCount =
            List.length (Splitter.getCompleted state.splitterState)

        allCompleted =
            Splitter.getCompleted newSplitterState

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
            Splitter.finish state.splitterState

        -- Get any remaining completed blocks
        previousCompletedCount =
            List.length (Splitter.getCompleted state.splitterState)

        allCompleted =
            Splitter.getCompleted finalSplitterState

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
    Splitter.getPending state.splitterState


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
    let
        -- First, render completed blocks
        renderedCompleted =
            blocks
                |> List.map renderChatMarkBlock

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
renderChatMarkBlock : ChatMarkBlock -> Html msg
renderChatMarkBlock block =
    case block of
        CompleteBlock _ parsedBlock ->
            renderMarkdownBlock parsedBlock

        PendingBlock raw ->
            renderPending raw

        ErrorBlock raw _ ->
            -- Just show as raw text, silently
            renderPending raw


{-| Render a parsed markdown block.
-}
renderMarkdownBlock : Markdown.Block.Block -> Html msg
renderMarkdownBlock block =
    case Markdown.Renderer.render Markdown.StyledRenderer.renderer [ block ] of
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
