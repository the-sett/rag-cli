module Markdown.StyledRenderer exposing (renderer)

{-| Custom markdown renderer that outputs Html.Styled elements.

This allows markdown to be rendered with elm-css styling.

-}

import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Markdown.Block exposing (ListItem(..), Task(..))
import Markdown.Html
import Markdown.Renderer exposing (Renderer)


{-| A renderer that produces Html.Styled elements.
-}
renderer : Renderer (Html msg)
renderer =
    { heading = renderHeading
    , paragraph = HS.p [ HA.class "md-paragraph" ]
    , blockQuote = HS.blockquote [ HA.class "md-blockquote" ]
    , html = Markdown.Html.oneOf []
    , text = HS.text
    , codeSpan = \code -> HS.code [ HA.class "md-code-inline" ] [ HS.text code ]
    , strong = HS.strong [ HA.class "md-strong" ]
    , emphasis = HS.em [ HA.class "md-emphasis" ]
    , strikethrough = HS.del [ HA.class "md-strikethrough" ]
    , hardLineBreak = HS.br [] []
    , link = renderLink
    , image = renderImage
    , unorderedList = renderUnorderedList
    , orderedList = renderOrderedList
    , codeBlock = renderCodeBlock
    , thematicBreak = HS.hr [ HA.class "md-hr" ] []
    , table = HS.table [ HA.class "md-table" ]
    , tableHeader = HS.thead [ HA.class "md-thead" ]
    , tableBody = HS.tbody [ HA.class "md-tbody" ]
    , tableRow = HS.tr [ HA.class "md-tr" ]
    , tableCell = renderTableCell
    , tableHeaderCell = renderTableHeaderCell
    }


renderHeading :
    { level : Markdown.Block.HeadingLevel
    , rawText : String
    , children : List (Html msg)
    }
    -> Html msg
renderHeading { level, children } =
    let
        tag =
            case level of
                Markdown.Block.H1 ->
                    HS.h1

                Markdown.Block.H2 ->
                    HS.h2

                Markdown.Block.H3 ->
                    HS.h3

                Markdown.Block.H4 ->
                    HS.h4

                Markdown.Block.H5 ->
                    HS.h5

                Markdown.Block.H6 ->
                    HS.h6
    in
    tag [ HA.class "md-heading" ] children


renderLink :
    { title : Maybe String
    , destination : String
    }
    -> List (Html msg)
    -> Html msg
renderLink { title, destination } children =
    HS.a
        ([ HA.href destination
         , HA.class "md-link"
         ]
            ++ (case title of
                    Just t ->
                        [ HA.title t ]

                    Nothing ->
                        []
               )
        )
        children


renderImage :
    { alt : String
    , src : String
    , title : Maybe String
    }
    -> Html msg
renderImage { alt, src, title } =
    HS.img
        ([ HA.src src
         , HA.alt alt
         , HA.class "md-image"
         ]
            ++ (case title of
                    Just t ->
                        [ HA.title t ]

                    Nothing ->
                        []
               )
        )
        []


renderUnorderedList : List (ListItem (Html msg)) -> Html msg
renderUnorderedList items =
    HS.ul [ HA.class "md-ul" ]
        (List.map renderListItem items)


renderListItem : ListItem (Html msg) -> Html msg
renderListItem (ListItem task children) =
    let
        taskCheckbox =
            case task of
                NoTask ->
                    []

                IncompleteTask ->
                    [ HS.input
                        [ HA.type_ "checkbox"
                        , HA.disabled True
                        , HA.class "md-task-checkbox"
                        ]
                        []
                    ]

                CompletedTask ->
                    [ HS.input
                        [ HA.type_ "checkbox"
                        , HA.disabled True
                        , HA.checked True
                        , HA.class "md-task-checkbox"
                        ]
                        []
                    ]
    in
    HS.li [ HA.class "md-li" ] (taskCheckbox ++ children)


renderOrderedList : Int -> List (List (Html msg)) -> Html msg
renderOrderedList startingNumber items =
    HS.ol
        [ HA.class "md-ol"
        , HA.start startingNumber
        ]
        (List.map (\children -> HS.li [ HA.class "md-li" ] children) items)


renderCodeBlock : { body : String, language : Maybe String } -> Html msg
renderCodeBlock { body, language } =
    let
        languageClass =
            case language of
                Just lang ->
                    [ HA.class ("language-" ++ lang) ]

                Nothing ->
                    []
    in
    HS.pre [ HA.class "md-pre" ]
        [ HS.code
            (HA.class "md-code-block" :: languageClass)
            [ HS.text body ]
        ]


renderTableCell :
    Maybe Markdown.Block.Alignment
    -> List (Html msg)
    -> Html msg
renderTableCell maybeAlignment children =
    HS.td
        [ HA.class "md-td"
        , alignmentToAttribute maybeAlignment
        ]
        children


renderTableHeaderCell :
    Maybe Markdown.Block.Alignment
    -> List (Html msg)
    -> Html msg
renderTableHeaderCell maybeAlignment children =
    HS.th
        [ HA.class "md-th"
        , alignmentToAttribute maybeAlignment
        ]
        children


alignmentToAttribute : Maybe Markdown.Block.Alignment -> HS.Attribute msg
alignmentToAttribute maybeAlignment =
    case maybeAlignment of
        Just Markdown.Block.AlignLeft ->
            HA.style "text-align" "left"

        Just Markdown.Block.AlignCenter ->
            HA.style "text-align" "center"

        Just Markdown.Block.AlignRight ->
            HA.style "text-align" "right"

        Nothing ->
            HA.class ""
