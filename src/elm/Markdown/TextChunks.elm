module Markdown.TextChunks exposing
    ( TextChunks
    , feed
    , finish
    , getCompleted
    , getPending
    , init
    )

{-| State machine for splitting streaming markdown text into blocks.

Handles detection of block boundaries including:

  - Blank lines (paragraph separators)
  - Headings (ATX style with #)
  - Code fences (\`\`\` or ~~~)
  - Lists (unordered and ordered)
  - Blockquotes
  - Thematic breaks
  - Tables

-}

import Regex exposing (Regex)


{-| The state of the block splitter.
-}
type alias TextChunks =
    { completed : List String -- Completed raw block strings (in order)
    , pending : String -- Current accumulating text
    , lineBuffer : String -- Partial line not yet terminated with \n
    , mode : Mode
    }


{-| The current parsing mode.
-}
type Mode
    = Normal
    | InCodeFence FenceInfo
    | InTable


{-| Information about the current code fence.
-}
type alias FenceInfo =
    { char : Char -- '`' or '~'
    , length : Int -- Number of fence chars (3+)
    , indent : Int -- Leading spaces before fence
    }


{-| Initialize a new splitter state.
-}
init : TextChunks
init =
    { completed = []
    , pending = ""
    , lineBuffer = ""
    , mode = Normal
    }


{-| Feed a chunk of text into the splitter.
-}
feed : String -> TextChunks -> TextChunks
feed delta state =
    let
        -- Combine line buffer with new delta
        combined =
            state.lineBuffer ++ delta

        -- Split into complete lines + remainder
        ( completeLines, remainder ) =
            splitLines combined
    in
    -- Process each complete line through state machine
    List.foldl processLine { state | lineBuffer = remainder } completeLines


{-| Finish processing and return final state.
Call this when the stream is done.
-}
finish : TextChunks -> TextChunks
finish state =
    let
        -- Process any remaining line buffer
        stateWithLastLine =
            if String.isEmpty state.lineBuffer then
                state

            else
                processLine state.lineBuffer { state | lineBuffer = "" }

        -- Complete any pending block
        finalState =
            completePendingBlock stateWithLastLine
    in
    finalState


{-| Get the list of completed block strings.
-}
getCompleted : TextChunks -> List String
getCompleted state =
    state.completed


{-| Get the current pending block string.
-}
getPending : TextChunks -> String
getPending state =
    state.pending



-- Internal helpers


{-| Split text into complete lines and a remainder.
A complete line ends with \\n.
-}
splitLines : String -> ( List String, String )
splitLines text =
    let
        parts =
            String.split "\n" text
    in
    case List.reverse parts of
        [] ->
            ( [], "" )

        [ only ] ->
            -- No newline found
            ( [], only )

        last :: rest ->
            -- Last part is remainder (may be empty if text ends with \n)
            ( List.reverse rest, last )


{-| Process a single complete line (without the trailing \\n).
-}
processLine : String -> TextChunks -> TextChunks
processLine line state =
    case state.mode of
        InCodeFence fenceInfo ->
            processLineInCodeFence line fenceInfo state

        InTable ->
            processLineInTable line state

        Normal ->
            processLineNormal line state


{-| Process a line while in code fence mode.
-}
processLineInCodeFence : String -> FenceInfo -> TextChunks -> TextChunks
processLineInCodeFence line fenceInfo state =
    if isClosingFence line fenceInfo then
        -- Close the code fence - include the closing fence in the block
        { state
            | completed = state.completed ++ [ state.pending ++ line ]
            , pending = ""
            , mode = Normal
        }

    else
        -- Continue accumulating code
        { state | pending = state.pending ++ line ++ "\n" }


{-| Check if a line is a closing fence matching the opening fence.
-}
isClosingFence : String -> FenceInfo -> Bool
isClosingFence line fenceInfo =
    let
        trimmed =
            String.trimLeft line

        indent =
            String.length line - String.length trimmed

        fenceChar =
            String.left 1 trimmed

        fenceLength =
            countLeadingChars fenceInfo.char trimmed

        afterFence =
            String.dropLeft fenceLength trimmed |> String.trim
    in
    -- Must be same char, at least same length, and only whitespace after
    (fenceChar == String.fromChar fenceInfo.char)
        && (fenceLength >= fenceInfo.length)
        && String.isEmpty afterFence
        && (indent <= 3)


{-| Count leading occurrences of a character.
-}
countLeadingChars : Char -> String -> Int
countLeadingChars char str =
    str
        |> String.toList
        |> List.foldl
            (\c ( count, counting ) ->
                if counting && c == char then
                    ( count + 1, True )

                else
                    ( count, False )
            )
            ( 0, True )
        |> Tuple.first


{-| Process a line while in table mode.
-}
processLineInTable : String -> TextChunks -> TextChunks
processLineInTable line state =
    if isTableRow line then
        -- Continue the table
        { state | pending = state.pending ++ line ++ "\n" }

    else
        -- Table ended, complete it and process this line normally
        let
            stateWithCompletedTable =
                completePendingBlock state
        in
        processLineNormal line stateWithCompletedTable


{-| Check if a line looks like a table row.
-}
isTableRow : String -> Bool
isTableRow line =
    let
        trimmed =
            String.trim line
    in
    String.startsWith "|" trimmed || isTableSeparator trimmed


{-| Check if a line is a table separator (|---|---|).
-}
isTableSeparator : String -> Bool
isTableSeparator line =
    let
        trimmed =
            String.trim line
    in
    String.startsWith "|" trimmed
        && String.contains "-" trimmed
        && String.all (\c -> c == '|' || c == '-' || c == ':' || c == ' ') trimmed


{-| Process a line in normal mode.
-}
processLineNormal : String -> TextChunks -> TextChunks
processLineNormal line state =
    let
        trimmed =
            String.trim line
    in
    if String.isEmpty trimmed then
        -- Blank line - complete current block
        completePendingBlock state

    else if isCodeFenceStart line then
        -- Start code fence
        let
            stateWithCompleted =
                completePendingBlock state

            fenceInfo =
                parseCodeFence line
        in
        { stateWithCompleted
            | pending = line ++ "\n"
            , mode = InCodeFence fenceInfo
        }

    else if isTableRow line && String.isEmpty state.pending then
        -- Start a table (only if we're not already in a block)
        { state
            | pending = line ++ "\n"
            , mode = InTable
        }

    else if isThematicBreak trimmed then
        -- Thematic break is its own block
        let
            stateWithCompleted =
                completePendingBlock state
        in
        { stateWithCompleted
            | completed = stateWithCompleted.completed ++ [ line ]
        }

    else if isAtxHeading line then
        -- Heading is its own block
        let
            stateWithCompleted =
                completePendingBlock state
        in
        { stateWithCompleted
            | completed = stateWithCompleted.completed ++ [ line ]
        }

    else if isListItemStart line && not (String.isEmpty state.pending) && not (isListItemContinuation state.pending) then
        -- New list item after non-list content
        let
            stateWithCompleted =
                completePendingBlock state
        in
        { stateWithCompleted
            | pending = line ++ "\n"
        }

    else if isBlockquoteStart line && not (String.isEmpty state.pending) && not (isBlockquoteContinuation state.pending) then
        -- New blockquote after non-blockquote content
        let
            stateWithCompleted =
                completePendingBlock state
        in
        { stateWithCompleted
            | pending = line ++ "\n"
        }

    else
        -- Continue accumulating into pending block
        { state | pending = state.pending ++ line ++ "\n" }


{-| Complete the current pending block if non-empty.
-}
completePendingBlock : TextChunks -> TextChunks
completePendingBlock state =
    let
        trimmedPending =
            String.trim state.pending
    in
    if String.isEmpty trimmedPending then
        { state | pending = "" }

    else
        { state
            | completed = state.completed ++ [ String.trimRight state.pending ]
            , pending = ""
        }


{-| Check if a line starts a code fence.
-}
isCodeFenceStart : String -> Bool
isCodeFenceStart line =
    let
        trimmed =
            String.trimLeft line

        indent =
            String.length line - String.length trimmed
    in
    (indent <= 3)
        && (String.startsWith "```" trimmed || String.startsWith "~~~" trimmed)


{-| Parse code fence info from a line.
-}
parseCodeFence : String -> FenceInfo
parseCodeFence line =
    let
        trimmed =
            String.trimLeft line

        indent =
            String.length line - String.length trimmed

        char =
            String.left 1 trimmed
                |> String.toList
                |> List.head
                |> Maybe.withDefault '`'

        length =
            countLeadingChars char trimmed
    in
    { char = char
    , length = length
    , indent = indent
    }


{-| Check if a line is a thematic break.
-}
isThematicBreak : String -> Bool
isThematicBreak line =
    let
        withoutSpaces =
            String.filter (\c -> c /= ' ') line

        firstChar =
            String.left 1 withoutSpaces
    in
    (String.length withoutSpaces >= 3)
        && (firstChar == "-" || firstChar == "*" || firstChar == "_")
        && String.all (\c -> c == '-' || c == '*' || c == '_') withoutSpaces
        && (String.all ((==) '-') withoutSpaces
                || String.all ((==) '*') withoutSpaces
                || String.all ((==) '_') withoutSpaces
           )


{-| Check if a line is an ATX heading.
-}
isAtxHeading : String -> Bool
isAtxHeading line =
    let
        trimmed =
            String.trimLeft line

        hashCount =
            countLeadingChars '#' trimmed

        afterHashes =
            String.dropLeft hashCount trimmed
    in
    (hashCount >= 1)
        && (hashCount <= 6)
        && (String.isEmpty afterHashes || String.startsWith " " afterHashes)


{-| Check if a line starts a list item.
-}
isListItemStart : String -> Bool
isListItemStart line =
    isUnorderedListItem line || isOrderedListItem line


{-| Check if a line starts an unordered list item.
-}
isUnorderedListItem : String -> Bool
isUnorderedListItem line =
    let
        trimmed =
            String.trimLeft line
    in
    (String.startsWith "- " trimmed
        || String.startsWith "* " trimmed
        || String.startsWith "+ " trimmed
    )
        && not (isThematicBreak (String.trim line))


{-| Check if a line starts an ordered list item.
-}
isOrderedListItem : String -> Bool
isOrderedListItem line =
    let
        trimmed =
            String.trimLeft line
    in
    case orderedListRegex of
        Just regex ->
            Regex.contains regex trimmed

        Nothing ->
            False


orderedListRegex : Maybe Regex
orderedListRegex =
    Regex.fromString "^\\d{1,9}[.)] "


{-| Check if the pending block appears to be a list.
-}
isListItemContinuation : String -> Bool
isListItemContinuation pending =
    case String.lines pending |> List.head of
        Just firstLine ->
            isListItemStart firstLine

        Nothing ->
            False


{-| Check if a line starts a blockquote.
-}
isBlockquoteStart : String -> Bool
isBlockquoteStart line =
    String.trimLeft line |> String.startsWith ">"


{-| Check if the pending block appears to be a blockquote.
-}
isBlockquoteContinuation : String -> Bool
isBlockquoteContinuation pending =
    case String.lines pending |> List.head of
        Just firstLine ->
            isBlockquoteStart firstLine

        Nothing ->
            False
