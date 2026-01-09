module Pages.Chat.Model exposing
    ( Model
    , ChatMessage
    , TocEntry
    , ScrollEvent
    , scrollEventDecoder
    )

{-| Model for the Chat page.
-}

import Json.Decode as Decode
import Markdown.ChatMarkBlock exposing (ChatMarkBlock, StreamState)


{-| The Chat page model.
-}
type alias Model =
    { userInput : String
    , pendingUserInput : Maybe String  -- Saved input for cancel/restore
    , messages : List ChatMessage
    , streamState : StreamState
    , isWaitingForResponse : Bool
    , tocEntriesHistory : List TocEntry
    , tocEntriesStreaming : List TocEntry
    , inputFocused : Bool
    , lastEnterTime : Int
    , activeTocEntryId : Maybe String
    , tocElementPositions : List { id : String, top : Float }
    , chatId : Maybe String  -- Current chat ID (Nothing for new chats)
    , sidebarVisible : Bool  -- Whether the sidebar is visible (controlled by MCP)
    , isDraggingOver : Bool  -- Whether a file is being dragged over the input
    }


{-| A chat message with role and content blocks.
-}
type alias ChatMessage =
    { role : String
    , blocks : List ChatMarkBlock
    }


{-| Table of contents entry derived from markdown headings or user queries.
-}
type alias TocEntry =
    { id : String
    , level : Int
    , text : String
    , messageIndex : Int
    , isUserQuery : Bool
    }


{-| Scroll event data decoded from the messages container.
-}
type alias ScrollEvent =
    { scrollTop : Float
    , clientHeight : Float
    }


{-| Decoder for scroll events on the messages container.
-}
scrollEventDecoder : Decode.Decoder ScrollEvent
scrollEventDecoder =
    Decode.map2 ScrollEvent
        (Decode.at [ "target", "scrollTop" ] Decode.float)
        (Decode.at [ "target", "clientHeight" ] Decode.float)
