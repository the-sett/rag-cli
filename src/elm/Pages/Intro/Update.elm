module Pages.Intro.Update exposing (Protocol, update)

{-| Update logic for the Intro page.
-}

import Pages.Intro.Model exposing (Model)
import Pages.Intro.Msg exposing (Msg(..))


{-| Protocol for communicating with the parent.
-}
type alias Protocol model msg =
    { toMsg : Msg -> msg
    , onUpdate : ( Model, Cmd msg ) -> ( model, Cmd msg )
    , onReady : ( Model, Cmd msg ) -> ( model, Cmd msg )
    }


{-| Update the Intro page model.
-}
update : Protocol model msg -> Msg -> Model -> ( model, Cmd msg )
update protocol msg model =
    case msg of
        Ready ->
            ( model, Cmd.none )
                |> protocol.onReady
