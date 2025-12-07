module Pages.Intro.Component exposing
    ( Model
    , Msg
    , Protocol
    , init
    , update
    , view
    , fetchChats
    )

{-| Intro page component.
-}

import Html.Styled exposing (Html)
import Pages.Intro.Model as Model
import Pages.Intro.Msg as Msg
import Pages.Intro.Update as Update
import Pages.Intro.View as View


type alias Model =
    Model.Model


type alias Msg =
    Msg.Msg


type alias Protocol model msg =
    Update.Protocol model msg


{-| Initialize the Intro page model.
-}
init : ( Model, Cmd msg )
init =
    ( { chats = []
      , loading = False
      , error = Nothing
      }
    , Cmd.none
    )


{-| Update the Intro page.
-}
update : Protocol model msg -> Msg -> Model -> ( model, Cmd msg )
update =
    Update.update


{-| Render the Intro page.
-}
view : View.Actions msg -> Model -> Html msg
view =
    View.view


{-| Fetch the chat list from the server.
-}
fetchChats : (Msg -> msg) -> Cmd msg
fetchChats =
    Update.fetchChats
