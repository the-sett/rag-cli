module Pages.Agents.Component exposing
    ( Model
    , Msg
    , Protocol
    , init
    , update
    , view
    , fetchAgents
    )

{-| Agents page component.
-}

import Html.Styled exposing (Html)
import Http
import Json.Decode as Decode
import Pages.Agents.Model as Model
import Pages.Agents.Msg as Msg
import Pages.Agents.Update as Update
import Pages.Agents.View as View


type alias Model =
    Model.Model


type alias Msg =
    Msg.Msg


type alias Protocol model msg =
    Update.Protocol model msg


{-| Initialize the Agents page model.
-}
init : ( Model, Cmd msg )
init =
    ( { userInput = ""
      , agents = []
      , selectedAgentId = Nothing
      , inputFocused = False
      , lastEnterTime = 0
      , loading = False
      , error = Nothing
      }
    , Cmd.none
    )


{-| Update the Agents page.
-}
update : Protocol model msg -> Msg -> Model -> ( model, Cmd msg )
update =
    Update.update


{-| Render the Agents page.
-}
view : View.Actions msg -> Model -> Html msg
view =
    View.view


{-| Fetch the list of agents from the backend.
-}
fetchAgents : (Msg -> msg) -> Cmd msg
fetchAgents toMsg =
    Http.get
        { url = "/api/agents"
        , expect = Http.expectJson (Msg.GotAgents >> toMsg) agentsDecoder
        }


agentsDecoder : Decode.Decoder (List Model.AgentInfo)
agentsDecoder =
    Decode.list agentDecoder


agentDecoder : Decode.Decoder Model.AgentInfo
agentDecoder =
    Decode.map4 Model.AgentInfo
        (Decode.field "id" Decode.string)
        (Decode.field "name" Decode.string)
        (Decode.field "instructions" Decode.string)
        (Decode.field "created_at" Decode.string)
