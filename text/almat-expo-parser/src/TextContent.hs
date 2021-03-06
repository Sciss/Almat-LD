{-# LANGUAGE DeriveGeneric         #-}
{-# LANGUAGE DuplicateRecordFields #-}

module TextContent
  ( TextBlock(..)
  , Link(..)
  , toTextBlock
  , hoverMeta
  ) where

import           Control.Monad
import           Control.Monad.Trans.Writer
import           Data.Aeson                 (ToJSON)
import qualified Data.Char                  as Char
import           Data.List                  (concat, filter, intercalate, null)
import           Data.Maybe                 (catMaybes, fromMaybe, mapMaybe)
import           Data.String.Utils          (endswith, startswith, strip)
import           Debug.Trace                (trace)
import           GHC.Generics
import           MetaData
import           Text.HTML.TagSoup          (Tag (..), parseTags, renderTags)

-- Utils for navigating parsed tags
takeUntil :: Tag String -> [Tag String] -> [Tag String]
takeUntil _ [] = []
takeUntil ending (a:_)
  | a == ending = [ending]
takeUntil ending (a:rest) = a : takeUntil ending rest

dropUntil :: Tag String -> [Tag String] -> [Tag String]
dropUntil _ [] = []
dropUntil ending (a:rest)
  | a == ending = rest
dropUntil ending (_:rest) = dropUntil ending rest

stripText :: [Tag String] -> [String]
stripText (TagText t:rest) = t : stripText rest
stripText (_:rest)         = stripText rest
stripText []               = []

removeNl :: [String] -> [String]
removeNl = filter (/= "\n")

remove160 :: String -> String
remove160 = filter (/= (Char.chr 160))

stripSpace :: String -> String
stripSpace = strip . remove160

concatNl :: [String] -> String
concatNl = intercalate "\n"

collectLinks :: [Tag String] -> [Link]
collectLinks [] = []
collectLinks (TagOpen "a" [("href", link)]:rest) =
  Link link (intercalate "\n" $ stripText $ takeUntil (TagClose "a") rest) :
  collectLinks (dropUntil (TagClose "a") rest)
collectLinks (_:rest) = collectLinks rest

-- Metadata extraction
type TextAndMeta = ([Tag String], [String])

extractMeta :: [Tag String] -> [Tag String] -> [String] -> TextAndMeta
extractMeta (TagText "---":rest) txt meta =
  extractMeta
    (dropUntil (TagText "---") rest)
    txt
    (concatNl (collectYamlString rest) : meta)
extractMeta (TagText t:rest) txt meta
  | let stripped = strip t
     in startswith "{" stripped && endswith "}" stripped =
    extractMeta rest txt (strip t : meta)
extractMeta (a:rest) txt meta = extractMeta rest (a : txt) meta
extractMeta [] txt meta = (reverse txt, reverse meta)

collectYamlString :: [Tag String] -> [String]
collectYamlString (TagText "---":_)  = []
collectYamlString []                 = []
collectYamlString (TagText str:rest) = str : collectYamlString rest
collectYamlString (_:rest)           = collectYamlString rest

parseAndExtractMeta :: String -> TextAndMeta
parseAndExtractMeta = (\tags -> extractMeta tags [] []) . parseTags

-- Text Block Types
data TextBlock
  = MetaBlock ParsedMetaData
  | TextBlock
      { metaData :: ParsedMetaData
      , html     :: String
      , links    :: [Link]
      }
  | LinkBlock Link
  deriving (Generic, Show, Eq)

instance ToJSON TextBlock

data Link =
  Link
    { href :: String
    , text :: String
    }
  deriving (Generic, Show, Eq)

instance ToJSON Link

asLink :: TextAndMeta -> Maybe TextBlock
asLink (tags, []) =
  case collectLinks tags of
    [l] ->
      if text l == (concat . removeNl) (stripText tags)
        then Just $ LinkBlock l
        else Nothing
    _ -> Nothing
asLink _ = Nothing

isMeta :: TextAndMeta -> Bool
isMeta (t, m) = null (removeNl $ stripText t) && (not . null) m

toTextBlock :: String -> Writer [String] TextBlock
toTextBlock str = do
  let (txt, meta) = parseAndExtractMeta str
      linksInText = collectLinks txt
      meta160 = map remove160 meta
  parsedMeta <- (mconcat . catMaybes) <$> mapM decodeMetaData meta160
  if isMeta (txt, meta)
    then return $ MetaBlock parsedMeta
    else return $
         fromMaybe
           (TextBlock parsedMeta (renderTags txt) linksInText)
           (asLink (txt, meta))

hoverMeta :: String -> Writer [String] ParsedMetaData
hoverMeta str =
  let (_, meta) = parseAndExtractMeta str
   in (mconcat . catMaybes) <$> mapM decodeMetaData meta
