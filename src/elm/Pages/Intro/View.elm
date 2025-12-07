module Pages.Intro.View exposing (Actions, view)

{-| View for the Intro page.
-}

import Html.Styled as HS exposing (Html)
import Html.Styled.Attributes as HA
import Html.Styled.Events as HE
import Pages.Intro.Msg exposing (Msg(..))


{-| Actions provided by the parent for the view.
-}
type alias Actions msg =
    { toMsg : Msg -> msg
    }


{-| Render the Intro page.
-}
view : Actions msg -> Html msg
view actions =
    HS.div
        [ HA.class "intro-page" ]
        [ HS.div
            [ HA.class "intro-content" ]
            [ HS.h1
                [ HA.class "intro-title" ]
                [ HS.text "CRAG" ]
            , HS.p
                [ HA.class "intro-subtitle" ]
                [ HS.text "RAG CLI Web Interface" ]
            , HS.button
                [ HA.class "intro-ready-button"
                , HE.onClick (actions.toMsg Ready)
                ]
                [ HS.text "Ready" ]
            ]
        ]
