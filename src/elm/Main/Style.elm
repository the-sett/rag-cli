module Main.Style exposing (style)

{-| CSS styles for the CRAG web interface.

Uses elm-css to define styles following the Scottish Government Design System.
See: https://designsystem.gov.scot/

Color palette, typography, spacing, and responsive breakpoints are based on
the gov.scot design system for optimal readability and accessibility.

-}

import Css
import Css.Global
import Css.Media as Media


{-| All CSS styles for the main application.
-}
style : List Css.Global.Snippet
style =
    List.concat
        [ globalStyles
        , appContainerStyles
        , mainLayoutStyles
        , headerStyles
        , connectionStatusStyles
        , messagesStyles
        , messageStyles
        , inputStyles
        , markdownStyles
        , focusStyles
        , accessibilityStyles
        ]



-- =============================================================================
-- Design Tokens (Scottish Government Design System)
-- =============================================================================
-- Colors


colorBrand : Css.Color
colorBrand =
    Css.hex "0065bd"


colorBrandHover : Css.Color
colorBrandHover =
    Css.hex "00437e"


colorText : Css.Color
colorText =
    Css.hex "1a1a1a"


colorTextSecondary : Css.Color
colorTextSecondary =
    Css.hex "5e5e5e"


colorBackground : Css.Color
colorBackground =
    Css.hex "ffffff"


colorBackgroundSecondary : Css.Color
colorBackgroundSecondary =
    Css.hex "f8f8f8"


colorBackgroundTertiary : Css.Color
colorBackgroundTertiary =
    Css.hex "ebebeb"


colorBorder : Css.Color
colorBorder =
    Css.hex "b3b3b3"


colorBorderLight : Css.Color
colorBorderLight =
    Css.hex "ebebeb"


colorBorderDark : Css.Color
colorBorderDark =
    Css.hex "1a1a1a"


colorFocus : Css.Color
colorFocus =
    Css.hex "fdd522"


colorPositive : Css.Color
colorPositive =
    Css.hex "1a7032"


colorNegative : Css.Color
colorNegative =
    Css.hex "d32205"


colorCodeBackground : Css.Color
colorCodeBackground =
    Css.hex "282c34"


colorCodeText : Css.Color
colorCodeText =
    Css.hex "abb2bf"



-- Typography


fontStack : List String
fontStack =
    [ "Roboto", "Helvetica Neue", "Helvetica", "Arial", "sans-serif" ]


fontStackMono : List String
fontStackMono =
    [ "Roboto Mono", "Consolas", "Monaco", "Andale Mono", "monospace" ]



-- Spacing (8px grid)


space1 : Css.Rem
space1 =
    Css.rem 0.5


space2 : Css.Rem
space2 =
    Css.rem 1


space3 : Css.Rem
space3 =
    Css.rem 1.5


space4 : Css.Rem
space4 =
    Css.rem 2


space5 : Css.Rem
space5 =
    Css.rem 2.5


space6 : Css.Rem
space6 =
    Css.rem 3



-- Breakpoints


breakpointSmall : Float
breakpointSmall =
    480


breakpointMedium : Float
breakpointMedium =
    768


breakpointLarge : Float
breakpointLarge =
    992


breakpointXLarge : Float
breakpointXLarge =
    1200



-- Container widths at breakpoints


containerMedium : Css.Px
containerMedium =
    Css.px 736


containerLarge : Css.Px
containerLarge =
    Css.px 928


containerXLarge : Css.Px
containerXLarge =
    Css.px 1120



-- =============================================================================
-- Media Query Helpers
-- =============================================================================


mediaSmall : List Css.Style -> Css.Style
mediaSmall styles =
    Media.withMedia [ Media.only Media.screen [ Media.minWidth (Css.px breakpointSmall) ] ] styles


mediaMedium : List Css.Style -> Css.Style
mediaMedium styles =
    Media.withMedia [ Media.only Media.screen [ Media.minWidth (Css.px breakpointMedium) ] ] styles


mediaLarge : List Css.Style -> Css.Style
mediaLarge styles =
    Media.withMedia [ Media.only Media.screen [ Media.minWidth (Css.px breakpointLarge) ] ] styles


mediaXLarge : List Css.Style -> Css.Style
mediaXLarge styles =
    Media.withMedia [ Media.only Media.screen [ Media.minWidth (Css.px breakpointXLarge) ] ] styles



-- =============================================================================
-- Global Styles
-- =============================================================================


globalStyles : List Css.Global.Snippet
globalStyles =
    [ Css.Global.html
        [ Css.pct 100 |> Css.height
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.property "-webkit-font-smoothing" "antialiased"
        , Css.property "-moz-osx-font-smoothing" "grayscale"
        ]
    , Css.Global.body
        [ Css.pct 100 |> Css.height
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.fontFamilies fontStack
        , Css.fontSize (Css.px 16)
        , Css.lineHeight (Css.num 1.5)
        , Css.color colorText
        , Css.backgroundColor colorBackgroundSecondary

        -- Responsive body text
        , mediaMedium
            [ Css.fontSize (Css.px 19)
            , Css.lineHeight (Css.num 1.68)
            ]
        ]
    , Css.Global.everything
        [ Css.boxSizing Css.borderBox
        ]
    ]



-- =============================================================================
-- App Container Styles
-- =============================================================================


appContainerStyles : List Css.Global.Snippet
appContainerStyles =
    [ Css.Global.class "app-container"
        [ Css.margin Css.zero
        , Css.padding Css.zero
        , Css.vh 100 |> Css.height
        , Css.width (Css.pct 100)
        , Css.displayFlex
        , Css.flexDirection Css.column
        , Css.boxSizing Css.borderBox
        , Css.overflow Css.hidden
        ]
    ]



-- =============================================================================
-- Main Layout (Two-column: TOC sidebar + content)
-- =============================================================================


mainLayoutStyles : List Css.Global.Snippet
mainLayoutStyles =
    [ Css.Global.class "main-layout"
        [ Css.displayFlex
        , Css.flex (Css.int 1)
        , Css.overflow Css.hidden
        , Css.minHeight Css.zero
        ]

    -- TOC Sidebar (left 25%)
    , Css.Global.class "toc-sidebar"
        [ Css.display Css.none -- Hidden on mobile
        , Css.flexDirection Css.column
        , Css.width (Css.pct 25)
        , Css.minWidth (Css.px 200)
        , Css.maxWidth (Css.px 300)
        , Css.flexShrink Css.zero
        , Css.overflowY Css.auto
        , Css.overflowX Css.hidden
        , Css.borderRight3 (Css.px 1) Css.solid colorBorderLight
        , Css.backgroundColor colorBackgroundSecondary
        , Css.padding space2
        , mediaMedium
            [ Css.displayFlex
            ]
        ]
    , Css.Global.class "toc-title"
        [ Css.fontSize (Css.rem 0.875)
        , Css.fontWeight (Css.int 700)
        , Css.textTransform Css.uppercase
        , Css.letterSpacing (Css.em 0.05)
        , Css.color colorTextSecondary
        , Css.margin4 Css.zero Css.zero space2 Css.zero
        , Css.paddingBottom space1
        , Css.borderBottom3 (Css.px 1) Css.solid colorBorderLight
        ]
    , Css.Global.class "toc-list"
        [ Css.listStyle Css.none
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.flex (Css.int 1)
        , Css.overflowY Css.auto
        ]
    , Css.Global.class "toc-entry"
        [ Css.margin Css.zero
        , Css.padding Css.zero
        ]
    , Css.Global.class "toc-link"
        [ Css.display Css.block
        , Css.width (Css.pct 100)
        , Css.padding2 space1 space1
        , Css.border Css.zero
        , Css.backgroundColor Css.transparent
        , Css.color colorText
        , Css.fontSize (Css.rem 0.875)
        , Css.lineHeight (Css.num 1.4)
        , Css.textAlign Css.left
        , Css.cursor Css.pointer
        , Css.textDecoration Css.none
        , Css.borderRadius (Css.px 4)
        , Css.property "transition" "background-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBackgroundTertiary
            ]
        , Css.focus
            [ Css.outline Css.none
            , Css.backgroundColor colorFocus
            , Css.color colorText
            ]
        ]

    -- TOC indentation by heading level
    , Css.Global.class "toc-level-1"
        [ Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.paddingLeft space1
                , Css.fontWeight (Css.int 700)
                ]
            ]
        ]
    , Css.Global.class "toc-level-2"
        [ Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.paddingLeft space3
                , Css.fontWeight (Css.int 400)
                ]
            ]
        ]
    , Css.Global.class "toc-level-3"
        [ Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.paddingLeft space5
                , Css.fontSize (Css.rem 0.8125)
                ]
            ]
        ]
    , Css.Global.class "toc-level-4"
        [ Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.paddingLeft space6
                , Css.fontSize (Css.rem 0.8125)
                , Css.color colorTextSecondary
                ]
            ]
        ]
    , Css.Global.class "toc-level-5"
        [ Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.paddingLeft space6
                , Css.fontSize (Css.rem 0.75)
                , Css.color colorTextSecondary
                ]
            ]
        ]
    , Css.Global.class "toc-level-6"
        [ Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.paddingLeft space6
                , Css.fontSize (Css.rem 0.75)
                , Css.color colorTextSecondary
                ]
            ]
        ]
    , Css.Global.class "toc-empty"
        [ Css.color colorTextSecondary
        , Css.fontSize (Css.rem 0.875)
        , Css.fontStyle Css.italic
        , Css.padding space2
        ]

    -- Content column (right 75%)
    , Css.Global.class "content-column"
        [ Css.flex (Css.int 1)
        , Css.displayFlex
        , Css.flexDirection Css.column
        , Css.minWidth Css.zero
        , Css.overflow Css.hidden
        ]
    ]



-- =============================================================================
-- Header Styles
-- =============================================================================


headerStyles : List Css.Global.Snippet
headerStyles =
    [ Css.Global.class "header"
        [ Css.marginBottom space2
        , Css.paddingBottom space2
        , Css.borderBottom3 (Css.px 1) Css.solid colorBorderLight
        , mediaMedium
            [ Css.marginBottom space3
            , Css.paddingBottom space3
            ]
        ]
    , Css.Global.class "header-title"
        [ Css.color colorText
        , Css.margin4 Css.zero Css.zero space1 Css.zero
        , Css.fontSize (Css.rem 1.875)
        , Css.lineHeight (Css.num 1.33)
        , Css.fontWeight (Css.int 700)
        , mediaMedium
            [ Css.fontSize (Css.rem 2.75)
            , Css.lineHeight (Css.num 1.27)
            ]
        ]
    ]



-- =============================================================================
-- Connection Status Styles
-- =============================================================================


connectionStatusStyles : List Css.Global.Snippet
connectionStatusStyles =
    [ Css.Global.class "connection-status"
        [ Css.displayFlex
        , Css.alignItems Css.center
        , Css.property "gap" "0.5rem"
        , Css.fontSize (Css.rem 0.75)
        , Css.color colorTextSecondary
        , Css.marginBottom space2
        , Css.paddingBottom space2
        , Css.borderBottom3 (Css.px 1) Css.solid colorBorderLight
        , Css.flexWrap Css.wrap
        ]
    , Css.Global.class "status-indicator"
        [ Css.width (Css.px 8)
        , Css.height (Css.px 8)
        , Css.borderRadius (Css.pct 50)
        , Css.flexShrink Css.zero
        ]
    , Css.Global.class "status-disconnected"
        [ Css.backgroundColor colorNegative
        ]
    , Css.Global.class "status-connecting"
        [ Css.backgroundColor colorFocus
        ]
    , Css.Global.class "status-connected"
        [ Css.backgroundColor colorPositive
        ]
    , Css.Global.class "reconnect-button"
        [ Css.marginLeft Css.auto
        , Css.padding2 (Css.rem 0.25) space1
        , Css.cursor Css.pointer
        , Css.border3 (Css.px 1) Css.solid colorBorder
        , Css.borderRadius (Css.px 4)
        , Css.backgroundColor colorBackground
        , Css.color colorText
        , Css.fontFamilies fontStack
        , Css.fontSize (Css.rem 0.75)
        , Css.property "transition" "color 0.2s, background-color 0.2s, border-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBackgroundSecondary
            , Css.borderColor colorBorderDark
            ]
        ]
    ]



-- =============================================================================
-- Messages Area Styles
-- =============================================================================


messagesStyles : List Css.Global.Snippet
messagesStyles =
    [ Css.Global.class "messages-container"
        [ Css.flex (Css.int 1)
        , Css.overflowY Css.auto
        , Css.overflowX Css.hidden
        , Css.backgroundColor colorBackground
        , Css.minHeight Css.zero -- Important for flex scroll
        ]
    , Css.Global.class "messages-content"
        [ Css.displayFlex
        , Css.flexDirection Css.column
        , Css.padding space2
        , mediaMedium
            [ Css.padding space3
            ]
        ]

    -- Dynamic spacer: always provides 80vh of space after content.
    -- This ensures any message can be scrolled to the top of the viewport.
    -- As streaming content grows, user scrolls down through the spacer.
    , Css.Global.class "messages-spacer"
        [ Css.height (Css.vh 80)
        , Css.flexShrink Css.zero
        ]
    , Css.Global.class "messages-empty"
        [ Css.color colorTextSecondary
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , mediaMedium
            [ Css.fontSize (Css.rem 1.1875)
            , Css.lineHeight (Css.num 1.68)
            ]
        ]
    ]



-- =============================================================================
-- Individual Message Styles
-- =============================================================================


messageStyles : List Css.Global.Snippet
messageStyles =
    [ Css.Global.class "message"
        [ Css.marginBottom space2
        , Css.padding space2
        , Css.backgroundColor colorBackground
        , Css.color colorText
        , mediaMedium
            [ Css.marginBottom space3
            , Css.padding space3
            ]
        ]
    , Css.Global.class "message-user"
        [ Css.borderTop3 (Css.px 5) Css.solid colorBrand
        , Css.borderBottom3 (Css.px 5) Css.solid colorBrand
        ]
    , Css.Global.class "message-assistant"
        []
    , Css.Global.class "message-error"
        [ Css.backgroundColor colorNegative
        , Css.color colorBackground
        ]
    , Css.Global.class "message-content"
        [ Css.margin Css.zero
        , Css.property "word-wrap" "break-word"
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , mediaMedium
            [ Css.fontSize (Css.rem 1.1875)
            , Css.lineHeight (Css.num 1.68)
            ]
        ]
    ]



-- =============================================================================
-- Input Area Styles
-- =============================================================================


inputStyles : List Css.Global.Snippet
inputStyles =
    [ Css.Global.class "input-container"
        [ Css.displayFlex
        , Css.property "gap" "0.5rem"
        , Css.padding space2
        , Css.backgroundColor colorBackgroundSecondary
        , Css.borderTop3 (Css.px 1) Css.solid colorBorderLight
        , mediaMedium
            [ Css.property "gap" "1rem"
            , Css.padding space3
            ]
        ]
    , Css.Global.class "input-textarea"
        [ Css.flex (Css.int 1)
        , Css.padding space2
        , Css.border3 (Css.px 2) Css.solid colorBorder
        , Css.borderRadius (Css.px 4)
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , Css.resize Css.none
        , Css.fontFamilies fontStack
        , Css.color colorText
        , Css.backgroundColor colorBackground
        , Css.minHeight (Css.rem 3)
        , Css.property "transition" "border-color 0.2s, box-shadow 0.2s"
        , mediaMedium
            [ Css.fontSize (Css.rem 1.1875)
            , Css.lineHeight (Css.num 1.68)
            ]
        , Css.focus
            [ Css.outline Css.none
            , Css.borderColor colorFocus
            , Css.boxShadow5 Css.inset Css.zero (Css.px -3) Css.zero colorBorderDark
            , Css.backgroundColor colorFocus
            ]
        , Css.disabled
            [ Css.backgroundColor colorBackgroundTertiary
            , Css.cursor Css.notAllowed
            , Css.color colorTextSecondary
            ]
        ]
    , Css.Global.class "send-button"
        [ Css.padding2 space2 space3
        , Css.color colorBackground
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.fontSize (Css.rem 1)
        , Css.fontWeight (Css.int 700)
        , Css.fontFamilies fontStack
        , Css.minHeight (Css.rem 3)
        , Css.minWidth (Css.rem 5)
        , Css.property "transition" "background-color 0.2s, transform 0.1s"
        , mediaMedium
            [ Css.fontSize (Css.rem 1.1875)
            , Css.padding2 space2 space4
            ]
        ]
    , Css.Global.class "send-button-enabled"
        [ Css.backgroundColor colorBrand
        , Css.cursor Css.pointer
        , Css.hover
            [ Css.backgroundColor colorBrandHover
            ]
        , Css.active
            [ Css.property "transform" "translateY(1px)"
            ]
        ]
    , Css.Global.class "send-button-disabled"
        [ Css.backgroundColor colorBorder
        , Css.cursor Css.notAllowed
        ]
    ]



-- =============================================================================
-- Markdown Rendering Styles
-- =============================================================================


markdownStyles : List Css.Global.Snippet
markdownStyles =
    [ -- Paragraphs
      Css.Global.class "md-paragraph"
        [ Css.marginTop Css.zero
        , Css.marginBottom space2
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , mediaMedium
            [ Css.fontSize (Css.rem 1.1875)
            , Css.lineHeight (Css.num 1.68)
            , Css.marginBottom space3
            ]
        ]

    -- Headings
    , Css.Global.class "md-heading"
        [ Css.marginTop space3
        , Css.marginBottom space2
        , Css.fontWeight (Css.int 700)
        , Css.color colorText
        , mediaMedium
            [ Css.marginTop space4
            ]
        ]

    -- H1 in markdown content
    , Css.Global.selector ".message-content h1"
        [ Css.fontSize (Css.rem 1.875)
        , Css.lineHeight (Css.num 1.33)
        , mediaMedium
            [ Css.fontSize (Css.rem 2.75)
            , Css.lineHeight (Css.num 1.27)
            ]
        ]

    -- H2 in markdown content
    , Css.Global.selector ".message-content h2"
        [ Css.fontSize (Css.rem 1.375)
        , Css.lineHeight (Css.num 1.45)
        , mediaMedium
            [ Css.fontSize (Css.rem 1.875)
            , Css.lineHeight (Css.num 1.33)
            ]
        ]

    -- H3 in markdown content
    , Css.Global.selector ".message-content h3"
        [ Css.fontSize (Css.rem 1.1875)
        , Css.lineHeight (Css.num 1.26)
        , mediaMedium
            [ Css.fontSize (Css.rem 1.375)
            , Css.lineHeight (Css.num 1.45)
            ]
        ]

    -- H4-H6 in markdown content
    , Css.Global.selector ".message-content h4, .message-content h5, .message-content h6"
        [ Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , mediaMedium
            [ Css.fontSize (Css.rem 1.1875)
            , Css.lineHeight (Css.num 1.68)
            ]
        ]

    -- Blockquotes
    , Css.Global.class "md-blockquote"
        [ Css.margin2 space2 Css.zero
        , Css.paddingLeft space2
        , Css.borderLeft3 (Css.px 4) Css.solid colorBorder
        , Css.color colorTextSecondary
        , Css.fontWeight (Css.int 300)
        , mediaMedium
            [ Css.margin2 space3 Css.zero
            , Css.paddingLeft space3
            ]
        ]

    -- Inline code
    , Css.Global.class "md-code-inline"
        [ Css.fontFamilies fontStackMono
        , Css.backgroundColor colorBackgroundTertiary
        , Css.padding2 (Css.rem 0.125) (Css.rem 0.375)
        , Css.borderRadius (Css.px 3)
        , Css.fontSize (Css.em 0.9)
        , Css.color colorText
        ]

    -- Code blocks (pre wrapper)
    , Css.Global.class "md-pre"
        [ Css.margin2 space2 Css.zero
        , Css.padding space2
        , Css.backgroundColor colorCodeBackground
        , Css.borderRadius (Css.px 4)
        , Css.overflow Css.auto
        , Css.property "-webkit-overflow-scrolling" "touch"
        , mediaMedium
            [ Css.margin2 space3 Css.zero
            , Css.padding space3
            ]
        ]

    -- Code block content
    , Css.Global.class "md-code-block"
        [ Css.fontFamilies fontStackMono
        , Css.fontSize (Css.rem 0.875)
        , Css.lineHeight (Css.num 1.5)
        , Css.color colorCodeText
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.backgroundColor Css.transparent
        , mediaMedium
            [ Css.fontSize (Css.rem 1)
            ]
        ]

    -- Text formatting
    , Css.Global.class "md-strong"
        [ Css.fontWeight (Css.int 700)
        ]
    , Css.Global.class "md-emphasis"
        [ Css.fontStyle Css.italic
        ]
    , Css.Global.class "md-strikethrough"
        [ Css.textDecoration Css.lineThrough
        ]

    -- Links
    , Css.Global.class "md-link"
        [ Css.color colorBrand
        , Css.textDecoration Css.underline
        , Css.property "text-underline-offset" "0.16em"
        , Css.property "transition" "color 0.2s"
        , Css.hover
            [ Css.color colorBrandHover
            , Css.property "text-decoration-thickness" "0.16em"
            ]
        ]

    -- Images
    , Css.Global.class "md-image"
        [ Css.maxWidth (Css.pct 100)
        , Css.height Css.auto
        , Css.borderRadius (Css.px 4)
        ]

    -- Lists
    , Css.Global.class "md-ul"
        [ Css.margin2 space2 Css.zero
        , Css.paddingLeft space3
        , mediaMedium
            [ Css.margin2 space3 Css.zero
            , Css.paddingLeft space4
            ]
        ]
    , Css.Global.class "md-ol"
        [ Css.margin2 space2 Css.zero
        , Css.paddingLeft space3
        , mediaMedium
            [ Css.margin2 space3 Css.zero
            , Css.paddingLeft space4
            ]
        ]
    , Css.Global.class "md-li"
        [ Css.marginBottom space1
        , Css.paddingLeft space1
        ]

    -- Horizontal rule
    , Css.Global.class "md-hr"
        [ Css.margin2 space4 Css.zero
        , Css.border Css.zero
        , Css.borderTop3 (Css.px 1) Css.solid colorBorder
        , mediaMedium
            [ Css.margin2 space5 Css.zero
            ]
        ]

    -- Tables
    , Css.Global.class "md-table"
        [ Css.width (Css.pct 100)
        , Css.borderCollapse Css.collapse
        , Css.margin2 space2 Css.zero
        , Css.fontSize (Css.rem 0.875)
        , mediaMedium
            [ Css.margin2 space3 Css.zero
            , Css.fontSize (Css.rem 1)
            ]
        ]
    , Css.Global.class "md-thead"
        [ Css.backgroundColor colorBackgroundSecondary
        ]
    , Css.Global.class "md-tbody"
        []
    , Css.Global.class "md-tr"
        [ Css.borderBottom3 (Css.px 1) Css.solid colorBorderLight
        ]
    , Css.Global.class "md-th"
        [ Css.padding2 space1 space2
        , Css.fontWeight (Css.int 700)
        , Css.textAlign Css.left
        , Css.borderBottom3 (Css.px 2) Css.solid colorBorder
        , mediaMedium
            [ Css.padding2 space2 space2
            ]
        ]
    , Css.Global.class "md-td"
        [ Css.padding2 space1 space2
        , mediaMedium
            [ Css.padding2 space2 space2
            ]
        ]

    -- Task list checkboxes
    , Css.Global.class "md-task-checkbox"
        [ Css.marginRight space1
        , Css.property "transform" "scale(1.2)"
        ]

    -- Pending/streaming text
    , Css.Global.class "md-pending"
        [ Css.fontFamilies fontStackMono
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.whiteSpace Css.preWrap
        , Css.color colorTextSecondary
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , mediaMedium
            [ Css.fontSize (Css.rem 1.1875)
            , Css.lineHeight (Css.num 1.68)
            ]
        ]

    -- Error blocks
    , Css.Global.class "md-error"
        [ Css.fontFamilies fontStackMono
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.color colorNegative
        , Css.fontSize (Css.rem 0.875)
        ]
    ]



-- =============================================================================
-- Focus Styles (Accessibility)
-- =============================================================================


focusStyles : List Css.Global.Snippet
focusStyles =
    [ -- Global focus style for interactive elements
      Css.Global.selector "a:focus, button:focus, input:focus, textarea:focus, select:focus, [tabindex]:focus"
        [ Css.outline3 (Css.rem 0.125) Css.solid colorFocus
        , Css.outlineOffset (Css.px 2)
        ]

    -- Send button focus
    , Css.Global.selector ".send-button:focus"
        [ Css.outline3 (Css.rem 0.125) Css.solid colorFocus
        , Css.outlineOffset (Css.px 2)
        , Css.backgroundColor colorFocus
        , Css.color colorText
        ]

    -- Reconnect button focus
    , Css.Global.selector ".reconnect-button:focus"
        [ Css.outline3 (Css.rem 0.125) Css.solid colorFocus
        , Css.outlineOffset (Css.px 2)
        , Css.backgroundColor colorFocus
        , Css.color colorText
        , Css.borderColor colorBorderDark
        ]

    -- Link focus in markdown
    , Css.Global.selector ".md-link:focus"
        [ Css.outline Css.none
        , Css.backgroundColor colorFocus
        , Css.color colorText
        , Css.textDecoration Css.none
        , Css.boxShadow5 Css.inset Css.zero (Css.px -3) Css.zero colorBorderDark
        ]
    ]



-- =============================================================================
-- Accessibility Styles
-- =============================================================================


accessibilityStyles : List Css.Global.Snippet
accessibilityStyles =
    [ -- Reduced motion
      Css.Global.selector "@media (prefers-reduced-motion: reduce)"
        [ Css.property "*" "{ transition-duration: 0.01ms !important; animation-duration: 0.01ms !important; scroll-behavior: auto !important; }"
        ]

    -- High contrast mode support
    , Css.Global.selector "@media (forced-colors: active)"
        [ Css.property ".status-indicator" "{ border: 2px solid currentColor; }"
        , Css.property ".send-button" "{ border: 2px solid currentColor; }"
        ]

    -- Screen reader only content (utility class)
    , Css.Global.class "sr-only"
        [ Css.position Css.absolute
        , Css.width (Css.px 1)
        , Css.height (Css.px 1)
        , Css.padding Css.zero
        , Css.margin (Css.px -1)
        , Css.overflow Css.hidden
        , Css.property "clip" "rect(0, 0, 0, 0)"
        , Css.whiteSpace Css.noWrap
        , Css.border Css.zero
        ]
    ]
