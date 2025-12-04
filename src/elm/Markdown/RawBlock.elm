module Markdown.RawBlock exposing
    ( RawBlock(..)
    , ListType(..)
    , assemble
    )

{-| Assembles raw block strings into grouped structures.

The main job is to group consecutive list items into a single list block,
and consecutive blockquote lines into a single blockquote block.

-}

import Regex exposing (Regex)


{-| A block after assembly, ready for parsing and rendering.
-}
type RawBlock
    = SingleBlock String
    | AssembledList ListType (List String)
    | AssembledBlockquote (List String)


{-| The type of list.
-}
type ListType
    = Unordered
    | Ordered Int -- starting number


{-| Assemble a list of raw block strings into grouped blocks.
-}
assemble : List String -> List RawBlock
assemble blocks =
    assembleHelper blocks []


assembleHelper : List String -> List RawBlock -> List RawBlock
assembleHelper remaining accumulated =
    case remaining of
        [] ->
            List.reverse accumulated

        block :: rest ->
            let
                blockType =
                    detectBlockType block
            in
            case blockType of
                UnorderedListBlock ->
                    let
                        ( listItems, afterList ) =
                            collectConsecutive isUnorderedListBlock remaining
                    in
                    assembleHelper afterList
                        (AssembledList Unordered listItems :: accumulated)

                OrderedListBlock startNum ->
                    let
                        ( listItems, afterList ) =
                            collectConsecutive isOrderedListBlock remaining
                    in
                    assembleHelper afterList
                        (AssembledList (Ordered startNum) listItems :: accumulated)

                BlockquoteBlock ->
                    let
                        ( quoteBlocks, afterQuote ) =
                            collectConsecutive isBlockquoteBlock remaining
                    in
                    assembleHelper afterQuote
                        (AssembledBlockquote quoteBlocks :: accumulated)

                OtherBlock ->
                    assembleHelper rest
                        (SingleBlock block :: accumulated)


{-| Internal block type detection.
-}
type DetectedBlockType
    = UnorderedListBlock
    | OrderedListBlock Int
    | BlockquoteBlock
    | OtherBlock


{-| Detect what type of block a string is.
-}
detectBlockType : String -> DetectedBlockType
detectBlockType block =
    let
        firstLine =
            String.lines block |> List.head |> Maybe.withDefault ""

        trimmed =
            String.trimLeft firstLine
    in
    if isUnorderedListStart trimmed then
        UnorderedListBlock

    else
        case getOrderedListStart trimmed of
            Just num ->
                OrderedListBlock num

            Nothing ->
                if String.startsWith ">" trimmed then
                    BlockquoteBlock

                else
                    OtherBlock


{-| Check if a block is an unordered list item.
-}
isUnorderedListBlock : String -> Bool
isUnorderedListBlock block =
    let
        firstLine =
            String.lines block |> List.head |> Maybe.withDefault ""

        trimmed =
            String.trimLeft firstLine
    in
    isUnorderedListStart trimmed


{-| Check if a string starts an unordered list item.
-}
isUnorderedListStart : String -> Bool
isUnorderedListStart str =
    (String.startsWith "- " str
        || String.startsWith "* " str
        || String.startsWith "+ " str
    )
        && not (isThematicBreak str)


{-| Check if a block is an ordered list item.
-}
isOrderedListBlock : String -> Bool
isOrderedListBlock block =
    let
        firstLine =
            String.lines block |> List.head |> Maybe.withDefault ""

        trimmed =
            String.trimLeft firstLine
    in
    getOrderedListStart trimmed /= Nothing


{-| Get the starting number of an ordered list item, if it is one.
-}
getOrderedListStart : String -> Maybe Int
getOrderedListStart str =
    case orderedListRegex of
        Just regex ->
            Regex.find regex str
                |> List.head
                |> Maybe.andThen
                    (\match ->
                        match.submatches
                            |> List.head
                            |> Maybe.andThen identity
                            |> Maybe.andThen String.toInt
                    )

        Nothing ->
            Nothing


orderedListRegex : Maybe Regex
orderedListRegex =
    Regex.fromString "^(\\d{1,9})[.)] "


{-| Check if a block is a blockquote.
-}
isBlockquoteBlock : String -> Bool
isBlockquoteBlock block =
    let
        firstLine =
            String.lines block |> List.head |> Maybe.withDefault ""

        trimmed =
            String.trimLeft firstLine
    in
    String.startsWith ">" trimmed


{-| Check if a string is a thematic break.
-}
isThematicBreak : String -> Bool
isThematicBreak line =
    let
        trimmed =
            String.trim line

        withoutSpaces =
            String.filter (\c -> c /= ' ') trimmed

        firstChar =
            String.left 1 withoutSpaces
    in
    (String.length withoutSpaces >= 3)
        && (firstChar == "-" || firstChar == "*" || firstChar == "_")
        && (String.all ((==) '-') withoutSpaces
                || String.all ((==) '*') withoutSpaces
                || String.all ((==) '_') withoutSpaces
           )


{-| Collect consecutive blocks that match a predicate.
Returns the collected blocks and the remaining blocks.
-}
collectConsecutive : (String -> Bool) -> List String -> ( List String, List String )
collectConsecutive predicate blocks =
    collectConsecutiveHelper predicate blocks []


collectConsecutiveHelper : (String -> Bool) -> List String -> List String -> ( List String, List String )
collectConsecutiveHelper predicate remaining collected =
    case remaining of
        [] ->
            ( List.reverse collected, [] )

        block :: rest ->
            if predicate block then
                collectConsecutiveHelper predicate rest (block :: collected)

            else
                ( List.reverse collected, remaining )
