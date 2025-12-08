module Navigation exposing
    ( Route(..)
    , routeToString
    , locationHrefToRoute
    , pushUrl
    , onUrlChange
    , click
    )

{-| URL-based routing for the application.
-}

import Html.Styled as HS exposing (Attribute)
import Html.Styled.Events as HE
import Json.Decode as Decode
import Ports
import Url exposing (Url)
import Url.Parser as Parser exposing ((</>), Parser)


{-| Application routes.
-}
type Route
    = Intro
    | Chat (Maybe String)  -- Optional chat ID for existing conversations
    | Agents               -- Agents management page


{-| Convert a route to a URL path string.
-}
routeToString : Route -> String
routeToString route =
    case route of
        Intro ->
            "/intro"

        Chat Nothing ->
            "/chat"

        Chat (Just chatId) ->
            "/chat/" ++ chatId

        Agents ->
            "/agents"


{-| Parse a location href string into a route.
-}
locationHrefToRoute : String -> Maybe Route
locationHrefToRoute href =
    Url.fromString href
        |> Maybe.andThen (Parser.parse routeParser)


{-| URL parser for routes.
-}
routeParser : Parser (Route -> a) a
routeParser =
    Parser.oneOf
        [ Parser.map Intro Parser.top
        , Parser.map Intro (Parser.s "intro")
        , Parser.map (Chat Nothing) (Parser.s "chat")
        , Parser.map (\id -> Chat (Just id)) (Parser.s "chat" </> Parser.string)
        , Parser.map Agents (Parser.s "agents")
        ]


{-| Push a new URL to the browser history.
-}
pushUrl : String -> Cmd msg
pushUrl =
    Ports.pushUrl


{-| Subscribe to URL changes.
-}
onUrlChange : (String -> msg) -> Sub msg
onUrlChange =
    Ports.onUrlChange


{-| Click handler that prevents default and triggers navigation.
-}
click : msg -> Attribute msg
click msg =
    HE.preventDefaultOn "click" (Decode.succeed ( msg, True ))
