module Main.Style exposing (style)

{-| CSS styles for the CRAG web interface.

Uses elm-css to define styles following the Scottish Government Design System.
See: <https://designsystem.gov.scot/>

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
        , scrollbarStyles
        , appContainerStyles
        , introStyles
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


colorWarning : Css.Color
colorWarning =
    Css.hex "e67e22"


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
-- Scrollbar Styles
-- =============================================================================


scrollbarStyles : List Css.Global.Snippet
scrollbarStyles =
    [ -- Webkit/Blink browsers (Chrome, Safari, Edge)
      Css.Global.selector "::-webkit-scrollbar"
        [ Css.width (Css.px 8)
        , Css.height (Css.px 8)
        ]
    , Css.Global.selector "::-webkit-scrollbar-track"
        [ Css.backgroundColor Css.transparent
        ]
    , Css.Global.selector "::-webkit-scrollbar-thumb"
        [ Css.backgroundColor (Css.hex "d0d0d0")
        , Css.borderRadius (Css.px 4)
        ]
    , Css.Global.selector "::-webkit-scrollbar-thumb:hover"
        [ Css.backgroundColor (Css.hex "b0b0b0")
        ]

    -- Firefox
    , Css.Global.selector "*"
        [ Css.property "scrollbar-width" "thin"
        , Css.property "scrollbar-color" "#d0d0d0 transparent"
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
-- Intro Page Styles
-- =============================================================================


introStyles : List Css.Global.Snippet
introStyles =
    [ Css.Global.class "intro-page"
        [ Css.displayFlex
        , Css.flex (Css.int 1)
        , Css.flexDirection Css.column
        , Css.backgroundColor colorBackground
        , Css.overflow Css.auto
        ]

    -- Header section - compact at the top
    , Css.Global.class "intro-header"
        [ Css.textAlign Css.center
        , Css.padding2 space3 space4
        , Css.flexShrink Css.zero
        , Css.position Css.relative
        ]
    , Css.Global.class "intro-title"
        [ Css.fontSize (Css.rem 2)
        , Css.fontWeight (Css.int 700)
        , Css.color colorText
        , Css.margin4 Css.zero Css.zero space1 Css.zero
        , mediaMedium
            [ Css.fontSize (Css.rem 2.5)
            ]
        ]
    , Css.Global.class "intro-subtitle"
        [ Css.fontSize (Css.rem 1)
        , Css.color colorTextSecondary
        , Css.margin4 Css.zero Css.zero space2 Css.zero
        , mediaMedium
            [ Css.fontSize (Css.rem 1.25)
            ]
        ]
    , Css.Global.class "intro-buttons"
        [ Css.displayFlex
        , Css.property "gap" "1rem"
        , Css.justifyContent Css.center
        ]
    , Css.Global.class "intro-ready-button"
        [ Css.padding2 space1 space3
        , Css.fontSize (Css.rem 1)
        , Css.fontWeight (Css.int 700)
        , Css.color colorBackground
        , Css.backgroundColor colorBrand
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.cursor Css.pointer
        , Css.property "transition" "background-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBrandHover
            ]
        , mediaMedium
            [ Css.fontSize (Css.rem 1.25)
            , Css.padding2 space2 space4
            ]
        ]
    , Css.Global.class "intro-agents-button"
        [ Css.padding2 space1 space3
        , Css.fontSize (Css.rem 1)
        , Css.fontWeight (Css.int 700)
        , Css.color colorText
        , Css.backgroundColor colorBackgroundSecondary
        , Css.border3 (Css.px 2) Css.solid colorBorder
        , Css.borderRadius (Css.px 4)
        , Css.cursor Css.pointer
        , Css.property "transition" "background-color 0.2s, border-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBackgroundTertiary
            , Css.borderColor colorBorderDark
            ]
        , mediaMedium
            [ Css.fontSize (Css.rem 1.25)
            , Css.padding2 space2 space4
            ]
        ]

    -- Two-column layout for lists
    , Css.Global.class "intro-columns"
        [ Css.displayFlex
        , Css.flex (Css.int 1)
        , Css.flexDirection Css.column
        , Css.padding2 Css.zero space3
        , Css.property "gap" "1.5rem"
        , Css.overflow Css.auto
        , mediaMedium
            [ Css.flexDirection Css.row
            , Css.padding2 Css.zero space4
            ]
        ]
    , Css.Global.class "intro-column"
        [ Css.flex (Css.int 1)
        , Css.minWidth Css.zero
        , Css.padding space2
        , Css.backgroundColor colorBackgroundSecondary
        , Css.borderRadius (Css.px 8)
        ]
    , Css.Global.class "intro-column-title"
        [ Css.fontSize (Css.rem 1.125)
        , Css.fontWeight (Css.int 700)
        , Css.color colorText
        , Css.margin4 Css.zero Css.zero space2 Css.zero
        , Css.paddingBottom space1
        , Css.borderBottom3 (Css.px 1) Css.solid colorBorderLight
        , mediaMedium
            [ Css.fontSize (Css.rem 1.25)
            ]
        ]

    -- List styles (shared by chats and agents)
    , Css.Global.class "intro-list-loading"
        [ Css.color colorTextSecondary
        , Css.fontStyle Css.italic
        , Css.padding space2
        ]
    , Css.Global.class "intro-list-empty"
        [ Css.color colorTextSecondary
        , Css.fontStyle Css.italic
        , Css.padding space2
        ]
    , Css.Global.class "intro-list-items"
        [ Css.listStyle Css.none
        , Css.margin Css.zero
        , Css.padding Css.zero
        , Css.maxHeight (Css.vh 50)
        , Css.overflowY Css.auto
        ]
    , Css.Global.class "intro-list-item"
        [ Css.displayFlex
        , Css.alignItems Css.center
        , Css.property "gap" "0.5rem"
        , Css.padding2 space1 space2
        , Css.marginBottom space1
        , Css.backgroundColor colorBackground
        , Css.borderRadius (Css.px 4)
        , Css.cursor Css.pointer
        , Css.property "transition" "background-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBackgroundTertiary
            ]
        ]
    , Css.Global.class "intro-item-title"
        [ Css.fontSize (Css.rem 0.9375)
        , Css.fontWeight (Css.int 500)
        , Css.color colorText
        , Css.overflow Css.hidden
        , Css.textOverflow Css.ellipsis
        , Css.whiteSpace Css.noWrap
        , mediaMedium
            [ Css.fontSize (Css.rem 1)
            ]
        ]
    , Css.Global.class "intro-item-content"
        [ Css.flex (Css.int 1)
        , Css.minWidth Css.zero
        ]
    , Css.Global.class "intro-item-date"
        [ Css.fontSize (Css.rem 0.75)
        , Css.color colorTextSecondary
        , Css.marginTop (Css.rem 0.125)
        ]

    -- Delete button for chat items
    , Css.Global.class "intro-delete-button"
        [ Css.padding2 (Css.px 4) (Css.px 8)
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.backgroundColor Css.transparent
        , Css.cursor Css.pointer
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1)
        , Css.opacity (Css.num 0.5)
        , Css.property "transition" "opacity 0.2s, background-color 0.2s"
        , Css.flexShrink Css.zero
        , Css.hover
            [ Css.opacity (Css.num 1)
            , Css.backgroundColor colorBackgroundTertiary
            ]
        ]

    -- Modal styles
    , Css.Global.class "modal-overlay"
        [ Css.position Css.fixed
        , Css.top Css.zero
        , Css.left Css.zero
        , Css.right Css.zero
        , Css.bottom Css.zero
        , Css.backgroundColor (Css.rgba 0 0 0 0.5)
        , Css.displayFlex
        , Css.alignItems Css.center
        , Css.justifyContent Css.center
        , Css.property "z-index" "1000"
        ]
    , Css.Global.class "modal-content"
        [ Css.backgroundColor colorBackground
        , Css.borderRadius (Css.px 8)
        , Css.padding space3
        , Css.maxWidth (Css.px 400)
        , Css.width (Css.pct 90)
        , Css.boxShadow5 Css.zero (Css.px 4) (Css.px 20) Css.zero (Css.rgba 0 0 0 0.2)
        ]
    , Css.Global.class "modal-title"
        [ Css.fontSize (Css.rem 1.25)
        , Css.fontWeight (Css.int 700)
        , Css.color colorText
        , Css.margin4 Css.zero Css.zero space2 Css.zero
        ]
    , Css.Global.class "modal-message"
        [ Css.fontSize (Css.rem 1)
        , Css.color colorText
        , Css.margin4 Css.zero Css.zero space3 Css.zero
        , Css.lineHeight (Css.num 1.5)
        ]
    , Css.Global.class "modal-buttons"
        [ Css.displayFlex
        , Css.justifyContent Css.flexEnd
        , Css.property "gap" "1rem"
        ]
    , Css.Global.class "modal-cancel-button"
        [ Css.padding2 space1 space2
        , Css.fontSize (Css.rem 1)
        , Css.fontWeight (Css.int 500)
        , Css.color colorText
        , Css.backgroundColor colorBackgroundSecondary
        , Css.border3 (Css.px 1) Css.solid colorBorder
        , Css.borderRadius (Css.px 4)
        , Css.cursor Css.pointer
        , Css.property "transition" "background-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBackgroundTertiary
            ]
        ]
    , Css.Global.class "modal-delete-button"
        [ Css.padding2 space1 space2
        , Css.fontSize (Css.rem 1)
        , Css.fontWeight (Css.int 500)
        , Css.color colorBackground
        , Css.backgroundColor colorNegative
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.cursor Css.pointer
        , Css.property "transition" "background-color 0.2s"
        , Css.hover
            [ Css.backgroundColor (Css.hex "b01c04")
            ]
        ]
    , Css.Global.class "modal-save-button"
        [ Css.padding2 space1 space2
        , Css.fontSize (Css.rem 1)
        , Css.fontWeight (Css.int 500)
        , Css.color colorBackground
        , Css.backgroundColor colorBrand
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.cursor Css.pointer
        , Css.property "transition" "background-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBrandHover
            ]
        ]

    -- Settings button styles
    , Css.Global.class "settings-button"
        [ Css.position Css.absolute
        , Css.top space2
        , Css.right space2
        , Css.padding2 (Css.px 4) (Css.px 8)
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.backgroundColor Css.transparent
        , Css.cursor Css.pointer
        , Css.fontSize (Css.rem 1.5)
        , Css.lineHeight (Css.num 1)
        , Css.color colorTextSecondary
        , Css.property "transition" "color 0.2s, background-color 0.2s"
        , Css.hover
            [ Css.color colorText
            , Css.backgroundColor colorBackgroundTertiary
            ]
        ]
    , Css.Global.class "settings-button-sidebar"
        [ Css.padding2 Css.zero (Css.px 6)
        , Css.cursor Css.pointer
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.backgroundColor Css.transparent
        , Css.color colorTextSecondary
        , Css.fontFamilies fontStack
        , Css.fontSize (Css.rem 1.25)
        , Css.lineHeight (Css.num 1)
        , Css.property "transition" "color 0.2s, background-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBackgroundSecondary
            , Css.color colorText
            ]
        ]

    -- Settings form styles
    , Css.Global.class "settings-form"
        [ Css.margin4 Css.zero Css.zero space3 Css.zero
        ]
    , Css.Global.class "settings-label"
        [ Css.fontSize (Css.rem 1)
        , Css.fontWeight (Css.int 600)
        , Css.color colorText
        , Css.margin4 Css.zero Css.zero space1 Css.zero
        , Css.display Css.block
        ]
    , Css.Global.class "settings-radio-group"
        [ Css.displayFlex
        , Css.flexDirection Css.column
        , Css.property "gap" "0.5rem"
        ]
    , Css.Global.class "settings-radio-option"
        [ Css.displayFlex
        , Css.alignItems Css.center
        , Css.property "gap" "0.5rem"
        , Css.padding space1
        , Css.borderRadius (Css.px 4)
        , Css.cursor Css.pointer
        , Css.property "transition" "background-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBackgroundSecondary
            ]
        ]
    , Css.Global.class "settings-radio-option-selected"
        [ Css.backgroundColor colorBackgroundTertiary
        ]
    , Css.Global.class "settings-radio-input"
        [ Css.width (Css.px 18)
        , Css.height (Css.px 18)
        , Css.margin Css.zero
        , Css.cursor Css.pointer
        ]
    , Css.Global.class "settings-radio-text"
        [ Css.fontSize (Css.rem 1)
        , Css.color colorText
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
        [ Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , Css.fontWeight (Css.int 700)
        , Css.textTransform Css.uppercase
        , Css.letterSpacing (Css.em 0.05)
        , Css.color colorTextSecondary
        , Css.margin4 Css.zero Css.zero space1 Css.zero
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
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
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
                ]
            ]
        ]
    , Css.Global.class "toc-level-4"
        [ Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.paddingLeft space6
                , Css.color colorTextSecondary
                ]
            ]
        ]
    , Css.Global.class "toc-level-5"
        [ Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.paddingLeft space6
                , Css.color colorTextSecondary
                ]
            ]
        ]
    , Css.Global.class "toc-level-6"
        [ Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.paddingLeft space6
                , Css.color colorTextSecondary
                ]
            ]
        ]
    , Css.Global.class "toc-empty"
        [ Css.color colorTextSecondary
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , Css.fontStyle Css.italic
        , Css.padding space2
        ]

    -- User query TOC entries - thin blue borders, single line with ellipsis
    -- Border is on the li element so adjacent sibling selector works for collapsing
    , Css.Global.class "toc-entry-user"
        [ Css.borderTop3 (Css.px 2) Css.solid colorBrand
        , Css.borderBottom3 (Css.px 2) Css.solid colorBrand
        , Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.paddingLeft space1
                , Css.borderRadius Css.zero
                , Css.overflow Css.hidden
                , Css.textOverflow Css.ellipsis
                , Css.whiteSpace Css.noWrap
                , Css.color colorTextSecondary
                ]
            ]
        ]

    -- Collapse borders when user entries are adjacent - use negative margin
    , Css.Global.class "toc-entry-user-adjacent"
        [ Css.marginTop (Css.px -2)
        ]

    -- Active TOC entry (current position from click or scroll)
    , Css.Global.class "toc-entry-active"
        [ Css.Global.children
            [ Css.Global.class "toc-link"
                [ Css.backgroundColor colorFocus
                ]
            ]
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
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , Css.color colorTextSecondary
        , Css.marginBottom space1
        , Css.paddingBottom space1
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
    , Css.Global.class "home-button"
        [ Css.padding2 Css.zero (Css.px 6)
        , Css.cursor Css.pointer
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.backgroundColor Css.transparent
        , Css.color colorTextSecondary
        , Css.fontFamilies fontStack
        , Css.fontSize (Css.rem 1.25)
        , Css.lineHeight (Css.num 1)
        , Css.property "transition" "color 0.2s, background-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBackgroundSecondary
            , Css.color colorText
            ]
        ]
    , Css.Global.class "reconnect-button"
        [ Css.marginLeft Css.auto
        , Css.padding2 space1 space1
        , Css.cursor Css.pointer
        , Css.border3 (Css.px 1) Css.solid colorBorder
        , Css.borderRadius (Css.px 4)
        , Css.backgroundColor colorBackground
        , Css.color colorText
        , Css.fontFamilies fontStack
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
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
    [ -- Wrapper for messages container + fade overlay
      Css.Global.class "messages-wrapper"
        [ Css.flex (Css.int 1)
        , Css.position Css.relative
        , Css.minHeight Css.zero
        , Css.overflow Css.hidden
        ]
    , Css.Global.class "messages-container"
        [ Css.position Css.absolute
        , Css.top Css.zero
        , Css.left Css.zero
        , Css.right Css.zero
        , Css.bottom Css.zero
        , Css.overflowY Css.auto
        , Css.overflowX Css.hidden
        , Css.backgroundColor colorBackground
        ]

    -- Fade-out gradient at bottom of messages area
    , Css.Global.class "messages-fade"
        [ Css.position Css.absolute
        , Css.bottom Css.zero
        , Css.left Css.zero
        , Css.right Css.zero
        , Css.height (Css.px 16)
        , Css.property "background" "linear-gradient(to bottom, rgba(255,255,255,0), rgba(255,255,255,1))"
        , Css.pointerEvents Css.none
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
        [ Css.backgroundColor colorBackground
        , Css.borderTop3 (Css.px 5) Css.solid colorNegative
        , Css.borderBottom3 (Css.px 5) Css.solid colorNegative
        , Css.fontFamilies fontStackMono
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

    -- Message header with copy button
    , Css.Global.class "message-header"
        [ Css.displayFlex
        , Css.justifyContent Css.flexEnd
        , Css.marginBottom space1
        ]

    -- Copy button (shared style for messages and code blocks)
    , Css.Global.class "copy-button"
        [ Css.padding2 (Css.px 4) (Css.px 8)
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.backgroundColor Css.transparent
        , Css.cursor Css.pointer
        , Css.fontSize (Css.rem 0.875)
        , Css.lineHeight (Css.num 1)
        , Css.color colorTextSecondary
        , Css.opacity (Css.num 0.6)
        , Css.property "transition" "opacity 0.2s, background-color 0.2s, color 0.2s"
        , Css.hover
            [ Css.opacity (Css.num 1)
            , Css.backgroundColor colorBackgroundTertiary
            , Css.color colorText
            ]
        ]
    ]



-- =============================================================================
-- Input Area Styles
-- =============================================================================


inputStyles : List Css.Global.Snippet
inputStyles =
    [ -- Outer container with padding (like messages-content) and bottom margin
      Css.Global.class "input-container"
        [ Css.paddingTop Css.zero
        , Css.paddingLeft space2
        , Css.paddingRight space2
        , Css.paddingBottom (Css.px 8)
        , mediaMedium
            [ Css.paddingTop Css.zero
            , Css.paddingLeft space3
            , Css.paddingRight space3
            , Css.paddingBottom (Css.px 8)
            ]
        ]

    -- Wrapper with top/bottom borders like user messages
    , Css.Global.class "input-wrapper"
        [ Css.position Css.relative
        , Css.backgroundColor colorBackground
        , Css.borderTop3 (Css.px 5) Css.solid colorBorder
        , Css.borderBottom3 (Css.px 5) Css.solid colorBorder
        , Css.property "transition" "border-color 0.2s"
        ]

    -- Inactive state (grey borders) - disabled/waiting
    , Css.Global.class "input-wrapper-inactive"
        [ Css.borderTopColor colorBorder
        , Css.borderBottomColor colorBorder
        ]

    -- Ready state (orange borders) - enabled but not focused
    , Css.Global.class "input-wrapper-ready"
        [ Css.borderTopColor colorWarning
        , Css.borderBottomColor colorWarning
        ]

    -- Focused state (green borders) - actively being used
    , Css.Global.class "input-wrapper-focused"
        [ Css.borderTopColor colorPositive
        , Css.borderBottomColor colorPositive
        ]

    -- Textarea - full width, no borders, monospace font
    -- Has bottom padding to reserve space for toolbar (always present to keep height stable)
    -- Uses field-sizing: content to auto-grow with wrapped text
    -- Max height is 2/3 of viewport, scrollbar appears when exceeded
    -- scroll-padding-bottom keeps cursor visible above the toolbar when scrolling
    , Css.Global.class "input-textarea"
        [ Css.width (Css.pct 100)
        , Css.property "box-sizing" "border-box"
        , Css.padding space2
        , Css.paddingBottom (Css.rem 2.5)
        , Css.border Css.zero
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , Css.resize Css.none
        , Css.fontFamilies fontStackMono
        , Css.color colorText
        , Css.backgroundColor colorBackground
        , Css.outline Css.none
        , Css.property "field-sizing" "content"
        , Css.minHeight (Css.rem 2.5)
        , Css.maxHeight (Css.vh 66)
        , Css.overflowY Css.auto
        , Css.property "scroll-padding-bottom" "2.5rem"
        , mediaMedium
            [ Css.fontSize (Css.rem 1.1875)
            , Css.lineHeight (Css.num 1.68)
            ]
        , Css.focus
            [ Css.outline Css.none
            ]
        , Css.disabled
            [ Css.backgroundColor colorBackgroundTertiary
            , Css.cursor Css.notAllowed
            , Css.color colorTextSecondary
            ]
        ]

    -- Override global yellow focus style for input-textarea
    , Css.Global.selector ".input-textarea:focus"
        [ Css.outline Css.none
        , Css.property "outline" "none"
        ]

    -- Toolbar that appears at bottom when focused - positioned absolutely
    , Css.Global.class "input-toolbar"
        [ Css.position Css.absolute
        , Css.bottom Css.zero
        , Css.left Css.zero
        , Css.right Css.zero
        , Css.displayFlex
        , Css.justifyContent Css.flexEnd
        , Css.alignItems Css.center
        , Css.padding2 (Css.rem 0.25) space2
        , Css.backgroundColor colorBackground
        , Css.borderTop3 (Css.px 1) Css.solid colorBorderLight
        ]

    -- Small send button with arrow icon
    , Css.Global.class "input-send-button"
        [ Css.padding2 (Css.rem 0.25) (Css.rem 0.5)
        , Css.color colorBackground
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.fontSize (Css.rem 1)
        , Css.fontWeight (Css.int 700)
        , Css.lineHeight (Css.num 1)
        , Css.property "transition" "background-color 0.2s, transform 0.1s"
        ]
    , Css.Global.class "input-send-button-enabled"
        [ Css.backgroundColor colorPositive
        , Css.cursor Css.pointer
        , Css.hover
            [ Css.backgroundColor colorBrandHover
            ]
        , Css.active
            [ Css.property "transform" "translateY(1px)"
            ]
        ]
    , Css.Global.class "input-send-button-disabled"
        [ Css.backgroundColor colorBorder
        , Css.cursor Css.notAllowed
        ]

    -- New agent button in sidebar
    , Css.Global.class "new-agent-button"
        [ Css.width (Css.pct 100)
        , Css.padding2 space1 space2
        , Css.marginBottom space2
        , Css.fontSize (Css.rem 1)
        , Css.fontWeight (Css.int 700)
        , Css.color colorBackground
        , Css.backgroundColor colorBrand
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.cursor Css.pointer
        , Css.property "transition" "background-color 0.2s"
        , Css.hover
            [ Css.backgroundColor colorBrandHover
            ]
        ]

    -- Agent editor container
    , Css.Global.class "agent-editor"
        [ Css.displayFlex
        , Css.flexDirection Css.column
        , Css.flex (Css.int 1)
        , Css.padding space3
        , Css.backgroundColor colorBackground
        ]
    , Css.Global.class "agent-editor-title"
        [ Css.fontSize (Css.rem 1.5)
        , Css.fontWeight (Css.int 700)
        , Css.color colorText
        , Css.margin4 Css.zero Css.zero space2 Css.zero
        ]
    , Css.Global.class "agent-error"
        [ Css.padding space2
        , Css.marginBottom space2
        , Css.backgroundColor (Css.hex "fdd")
        , Css.color colorNegative
        , Css.borderRadius (Css.px 4)
        , Css.fontSize (Css.rem 1)
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
        , Css.position Css.relative
        , mediaMedium
            [ Css.margin2 space3 Css.zero
            , Css.padding space3
            ]
        ]

    -- Code block copy button (light on dark background)
    , Css.Global.class "code-copy-button"
        [ Css.position Css.absolute
        , Css.top (Css.px 8)
        , Css.right (Css.px 8)
        , Css.padding2 (Css.px 4) (Css.px 8)
        , Css.border Css.zero
        , Css.borderRadius (Css.px 4)
        , Css.backgroundColor Css.transparent
        , Css.cursor Css.pointer
        , Css.fontSize (Css.rem 0.875)
        , Css.lineHeight (Css.num 1)
        , Css.color colorCodeText
        , Css.opacity (Css.num 0.5)
        , Css.property "transition" "opacity 0.2s, background-color 0.2s"
        , Css.hover
            [ Css.opacity (Css.num 1)
            , Css.backgroundColor (Css.rgba 255 255 255 0.1)
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
        , Css.whiteSpace Css.preWrap
        , Css.color colorText
        , Css.fontSize (Css.rem 1)
        , Css.lineHeight (Css.num 1.5)
        , mediaMedium
            [ Css.fontSize (Css.rem 1.1875)
            , Css.lineHeight (Css.num 1.68)
            ]
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

    -- Home button focus
    , Css.Global.selector ".home-button:focus"
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
