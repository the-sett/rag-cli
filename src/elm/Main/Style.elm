module Main.Style exposing (style)

{-| CSS styles for the CRAG web interface.

Uses elm-css to define styles as classes that are applied in the view.

-}

import Css
import Css.Global


{-| All CSS styles for the main application.
-}
style : List Css.Global.Snippet
style =
    [ -- Global styles
      Css.Global.html
        [ Css.pct 100 |> Css.height
        , Css.margin Css.zero
        , Css.padding Css.zero
        ]
    , Css.Global.body
        [ Css.pct 100 |> Css.height
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.fontFamilies [ "system-ui", "-apple-system", "sans-serif" ]
        , Css.backgroundColor (Css.hex "f5f5f5")
        ]

    -- App container
    , Css.Global.class "app-container"
        [ Css.maxWidth (Css.px 800)
        , Css.margin2 Css.zero Css.auto
        , Css.padding (Css.rem 2)
        , Css.vh 100 |> Css.height
        , Css.displayFlex
        , Css.flexDirection Css.column
        , Css.boxSizing Css.borderBox
        ]

    -- Header
    , Css.Global.class "header"
        [ Css.marginBottom (Css.rem 1)
        , Css.paddingBottom (Css.rem 1)
        , Css.borderBottom3 (Css.px 1) Css.solid (Css.hex "eee")
        ]
    , Css.Global.class "header-title"
        [ Css.color (Css.hex "333")
        , Css.margin4 Css.zero Css.zero (Css.rem 0.5) Css.zero
        , Css.fontSize (Css.rem 1.5)
        ]

    -- Connection status
    , Css.Global.class "connection-status"
        [ Css.displayFlex
        , Css.alignItems Css.center
        , Css.property "gap" "0.5rem"
        ]
    , Css.Global.class "status-indicator"
        [ Css.width (Css.px 10)
        , Css.height (Css.px 10)
        , Css.borderRadius (Css.pct 50)
        ]
    , Css.Global.class "status-disconnected"
        [ Css.backgroundColor (Css.hex "dc3545")
        ]
    , Css.Global.class "status-connecting"
        [ Css.backgroundColor (Css.hex "ffc107")
        ]
    , Css.Global.class "status-connected"
        [ Css.backgroundColor (Css.hex "28a745")
        ]
    , Css.Global.class "reconnect-button"
        [ Css.marginLeft (Css.rem 1)
        , Css.padding2 (Css.rem 0.25) (Css.rem 0.5)
        , Css.cursor Css.pointer
        , Css.border3 (Css.px 1) Css.solid (Css.hex "ccc")
        , Css.borderRadius (Css.px 4)
        , Css.backgroundColor (Css.hex "fff")
        , Css.hover
            [ Css.backgroundColor (Css.hex "f0f0f0")
            ]
        ]

    -- Messages area
    , Css.Global.class "messages-container"
        [ Css.flex (Css.int 1)
        , Css.overflowY Css.auto
        , Css.marginBottom (Css.rem 1)
        , Css.padding (Css.rem 1)
        , Css.backgroundColor (Css.hex "f8f9fa")
        , Css.borderRadius (Css.px 8)
        ]
    , Css.Global.class "messages-empty"
        [ Css.color (Css.hex "666")
        ]

    -- Individual message
    , Css.Global.class "message"
        [ Css.marginBottom (Css.rem 1)
        , Css.padding2 (Css.rem 0.75) (Css.rem 1)
        , Css.borderRadius (Css.px 8)
        , Css.boxShadow4 Css.zero (Css.px 1) (Css.px 3) (Css.rgba 0 0 0 0.1)
        ]
    , Css.Global.class "message-user"
        [ Css.backgroundColor (Css.hex "007bff")
        , Css.color (Css.hex "fff")
        ]
    , Css.Global.class "message-assistant"
        [ Css.backgroundColor (Css.hex "fff")
        , Css.color (Css.hex "333")
        ]
    , Css.Global.class "message-error"
        [ Css.backgroundColor (Css.hex "dc3545")
        , Css.color (Css.hex "fff")
        ]
    , Css.Global.class "message-label"
        [ Css.fontWeight Css.bold
        , Css.marginBottom (Css.rem 0.25)
        , Css.fontSize (Css.rem 0.85)
        ]
    , Css.Global.class "message-content"
        [ Css.margin Css.zero
        , Css.whiteSpace Css.preWrap
        , Css.property "word-wrap" "break-word"
        , Css.fontFamily Css.inherit
        ]

    -- Input area
    , Css.Global.class "input-container"
        [ Css.displayFlex
        , Css.property "gap" "0.5rem"
        ]
    , Css.Global.class "input-textarea"
        [ Css.flex (Css.int 1)
        , Css.padding (Css.rem 0.75)
        , Css.border3 (Css.px 1) Css.solid (Css.hex "ddd")
        , Css.borderRadius (Css.px 8)
        , Css.fontSize (Css.rem 1)
        , Css.resize Css.none
        , Css.fontFamily Css.inherit
        , Css.focus
            [ Css.outline Css.none
            , Css.borderColor (Css.hex "007bff")
            ]
        , Css.disabled
            [ Css.backgroundColor (Css.hex "f5f5f5")
            , Css.cursor Css.notAllowed
            ]
        ]
    , Css.Global.class "send-button"
        [ Css.padding2 (Css.rem 0.75) (Css.rem 1.5)
        , Css.color (Css.hex "fff")
        , Css.border Css.zero
        , Css.borderRadius (Css.px 8)
        , Css.fontSize (Css.rem 1)
        ]
    , Css.Global.class "send-button-enabled"
        [ Css.backgroundColor (Css.hex "007bff")
        , Css.cursor Css.pointer
        , Css.hover
            [ Css.backgroundColor (Css.hex "0056b3")
            ]
        ]
    , Css.Global.class "send-button-disabled"
        [ Css.backgroundColor (Css.hex "ccc")
        , Css.cursor Css.notAllowed
        ]

    -- Markdown rendering styles
    , Css.Global.class "md-paragraph"
        [ Css.marginTop Css.zero
        , Css.marginBottom (Css.rem 1)
        , Css.lineHeight (Css.num 1.6)
        ]
    , Css.Global.class "md-heading"
        [ Css.marginTop (Css.rem 1.5)
        , Css.marginBottom (Css.rem 0.75)
        , Css.fontWeight Css.bold
        , Css.lineHeight (Css.num 1.25)
        ]
    , Css.Global.class "md-blockquote"
        [ Css.margin2 (Css.rem 1) Css.zero
        , Css.paddingLeft (Css.rem 1)
        , Css.borderLeft3 (Css.px 4) Css.solid (Css.hex "ddd")
        , Css.color (Css.hex "666")
        ]
    , Css.Global.class "md-code-inline"
        [ Css.fontFamily Css.monospace
        , Css.backgroundColor (Css.hex "f4f4f4")
        , Css.padding2 (Css.rem 0.125) (Css.rem 0.25)
        , Css.borderRadius (Css.px 3)
        , Css.fontSize (Css.em 0.9)
        ]
    , Css.Global.class "md-pre"
        [ Css.margin2 (Css.rem 1) Css.zero
        , Css.padding (Css.rem 1)
        , Css.backgroundColor (Css.hex "282c34")
        , Css.borderRadius (Css.px 6)
        , Css.overflow Css.auto
        ]
    , Css.Global.class "md-code-block"
        [ Css.fontFamily Css.monospace
        , Css.fontSize (Css.rem 0.875)
        , Css.lineHeight (Css.num 1.5)
        , Css.color (Css.hex "abb2bf")
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.backgroundColor Css.transparent
        ]
    , Css.Global.class "md-strong"
        [ Css.fontWeight Css.bold
        ]
    , Css.Global.class "md-emphasis"
        [ Css.fontStyle Css.italic
        ]
    , Css.Global.class "md-strikethrough"
        [ Css.textDecoration Css.lineThrough
        ]
    , Css.Global.class "md-link"
        [ Css.color (Css.hex "007bff")
        , Css.textDecoration Css.none
        , Css.hover
            [ Css.textDecoration Css.underline
            ]
        ]
    , Css.Global.class "md-image"
        [ Css.maxWidth (Css.pct 100)
        , Css.height Css.auto
        ]
    , Css.Global.class "md-ul"
        [ Css.margin2 (Css.rem 1) Css.zero
        , Css.paddingLeft (Css.rem 2)
        ]
    , Css.Global.class "md-ol"
        [ Css.margin2 (Css.rem 1) Css.zero
        , Css.paddingLeft (Css.rem 2)
        ]
    , Css.Global.class "md-li"
        [ Css.marginBottom (Css.rem 0.25)
        ]
    , Css.Global.class "md-hr"
        [ Css.margin2 (Css.rem 2) Css.zero
        , Css.border Css.zero
        , Css.borderTop3 (Css.px 1) Css.solid (Css.hex "ddd")
        ]
    , Css.Global.class "md-table"
        [ Css.width (Css.pct 100)
        , Css.borderCollapse Css.collapse
        , Css.margin2 (Css.rem 1) Css.zero
        ]
    , Css.Global.class "md-thead"
        [ Css.backgroundColor (Css.hex "f5f5f5")
        ]
    , Css.Global.class "md-tbody"
        []
    , Css.Global.class "md-tr"
        [ Css.borderBottom3 (Css.px 1) Css.solid (Css.hex "ddd")
        ]
    , Css.Global.class "md-th"
        [ Css.padding2 (Css.rem 0.5) (Css.rem 0.75)
        , Css.fontWeight Css.bold
        , Css.textAlign Css.left
        ]
    , Css.Global.class "md-td"
        [ Css.padding2 (Css.rem 0.5) (Css.rem 0.75)
        ]
    , Css.Global.class "md-task-checkbox"
        [ Css.marginRight (Css.rem 0.5)
        ]
    , Css.Global.class "md-pending"
        [ Css.fontFamily Css.monospace
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.whiteSpace Css.preWrap
        , Css.color (Css.hex "888")
        ]
    , Css.Global.class "md-error"
        [ Css.fontFamily Css.monospace
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.color (Css.hex "dc3545")
        ]
    ]
