module Pages.Agents.Update exposing
    ( Protocol
    , update
    )

{-| Update logic for the Agents page.
-}

import Http
import Json.Decode as Decode
import Json.Encode as Encode
import Pages.Agents.Model exposing (AgentInfo, Model)
import Pages.Agents.Msg exposing (Msg(..))


{-| Protocol for communicating with the parent.
-}
type alias Protocol model msg =
    { toMsg : Msg -> msg
    , onUpdate : ( Model, Cmd msg ) -> ( model, Cmd msg )
    }


{-| Update the Agents page model.
-}
update : Protocol model msg -> Msg -> Model -> ( model, Cmd msg )
update protocol msg model =
    case msg of
        UserInputChanged input ->
            ( { model | userInput = input }, Cmd.none )
                |> protocol.onUpdate

        SubmitAgent ->
            submitAgent protocol model

        SelectAgent agentId ->
            selectAgent protocol agentId model

        NewAgent ->
            ( { model
                | selectedAgentId = Nothing
                , userInput = ""
              }
            , Cmd.none
            )
                |> protocol.onUpdate

        InputFocused ->
            ( { model | inputFocused = True }, Cmd.none )
                |> protocol.onUpdate

        InputBlurred ->
            ( { model | inputFocused = False }, Cmd.none )
                |> protocol.onUpdate

        InputKeyDown currentTime ->
            handleInputKeyDown protocol currentTime model

        GotAgents result ->
            handleGotAgents protocol result model

        AgentSaved result ->
            handleAgentSaved protocol result model


-- Internal helpers


submitAgent : Protocol model msg -> Model -> ( model, Cmd msg )
submitAgent protocol model =
    if String.trim model.userInput /= "" then
        let
            -- Name is the first line of instructions
            name =
                model.userInput
                    |> String.lines
                    |> List.head
                    |> Maybe.withDefault model.userInput
                    |> String.trim

            -- Build the request body
            body =
                case model.selectedAgentId of
                    Just agentId ->
                        -- Update existing agent
                        Encode.object
                            [ ( "id", Encode.string agentId )
                            , ( "name", Encode.string name )
                            , ( "instructions", Encode.string model.userInput )
                            ]

                    Nothing ->
                        -- Create new agent
                        Encode.object
                            [ ( "name", Encode.string name )
                            , ( "instructions", Encode.string model.userInput )
                            ]

            request =
                Http.post
                    { url = "/api/agents"
                    , body = Http.jsonBody body
                    , expect = Http.expectJson (AgentSaved >> protocol.toMsg) agentDecoder
                    }
        in
        ( { model | loading = True, error = Nothing }
        , request
        )
            |> protocol.onUpdate

    else
        ( model, Cmd.none )
            |> protocol.onUpdate


selectAgent : Protocol model msg -> String -> Model -> ( model, Cmd msg )
selectAgent protocol agentId model =
    let
        selectedAgent =
            List.filter (\a -> a.id == agentId) model.agents
                |> List.head
    in
    case selectedAgent of
        Just agent ->
            ( { model
                | selectedAgentId = Just agentId
                , userInput = agent.instructions
              }
            , Cmd.none
            )
                |> protocol.onUpdate

        Nothing ->
            ( model, Cmd.none )
                |> protocol.onUpdate


handleInputKeyDown : Protocol model msg -> Int -> Model -> ( model, Cmd msg )
handleInputKeyDown protocol currentTime model =
    let
        timeDiff =
            currentTime - model.lastEnterTime
    in
    if timeDiff < 400 && timeDiff > 0 then
        submitAgent protocol { model | lastEnterTime = 0 }

    else
        ( { model | lastEnterTime = currentTime }, Cmd.none )
            |> protocol.onUpdate


handleGotAgents : Protocol model msg -> Result Http.Error (List AgentInfo) -> Model -> ( model, Cmd msg )
handleGotAgents protocol result model =
    case result of
        Ok agents ->
            ( { model | agents = agents, loading = False, error = Nothing }
            , Cmd.none
            )
                |> protocol.onUpdate

        Err err ->
            ( { model | loading = False, error = Just (httpErrorToString err) }
            , Cmd.none
            )
                |> protocol.onUpdate


handleAgentSaved : Protocol model msg -> Result Http.Error AgentInfo -> Model -> ( model, Cmd msg )
handleAgentSaved protocol result model =
    case result of
        Ok agent ->
            let
                -- Update the agents list
                updatedAgents =
                    if List.any (\a -> a.id == agent.id) model.agents then
                        List.map
                            (\a ->
                                if a.id == agent.id then
                                    agent

                                else
                                    a
                            )
                            model.agents

                    else
                        model.agents ++ [ agent ]
            in
            ( { model
                | agents = updatedAgents
                , selectedAgentId = Just agent.id
                , loading = False
                , error = Nothing
              }
            , Cmd.none
            )
                |> protocol.onUpdate

        Err err ->
            ( { model | loading = False, error = Just (httpErrorToString err) }
            , Cmd.none
            )
                |> protocol.onUpdate


httpErrorToString : Http.Error -> String
httpErrorToString error =
    case error of
        Http.BadUrl url ->
            "Bad URL: " ++ url

        Http.Timeout ->
            "Request timed out"

        Http.NetworkError ->
            "Network error"

        Http.BadStatus status ->
            "Server error: " ++ String.fromInt status

        Http.BadBody body ->
            "Bad response: " ++ body


agentDecoder : Decode.Decoder AgentInfo
agentDecoder =
    Decode.map4 AgentInfo
        (Decode.field "id" Decode.string)
        (Decode.field "name" Decode.string)
        (Decode.field "instructions" Decode.string)
        (Decode.field "created_at" Decode.string)
