port module Main exposing (main)

import Browser
import Html exposing (Html, div, h1, p, text)
import Html.Attributes exposing (style)


main : Program () Model Msg
main =
    Browser.element
        { init = init
        , update = update
        , view = view
        , subscriptions = subscriptions
        }


port consoleLog : String -> Cmd msg


type alias Model =
    {}


type Msg
    = NoOp


init : () -> ( Model, Cmd Msg )
init _ =
    ( {}
    , consoleLog "CRAG Elm application initialized"
    )


update : Msg -> Model -> ( Model, Cmd Msg )
update msg model =
    case msg of
        NoOp ->
            ( model, Cmd.none )


subscriptions : Model -> Sub Msg
subscriptions _ =
    Sub.none


view : Model -> Html Msg
view model =
    div
        [ style "font-family" "system-ui, -apple-system, sans-serif"
        , style "max-width" "800px"
        , style "margin" "0 auto"
        , style "padding" "2rem"
        ]
        [ h1
            [ style "color" "#333"
            ]
            [ text "CRAG Web Interface" ]
        , p
            [ style "color" "#666"
            , style "font-size" "1.2rem"
            ]
            [ text "Hello from Elm! The web interface is working." ]
        ]
