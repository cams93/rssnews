/*
 ============================================================================
 Name        : test.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>

#include "url.h"
#include "bool.h"
#include "urlconnection.h"
#include "streamtokenizer.h"
#include "html-utils.h"
#include "hashset.h"
#include "rss-news-search.h"

/**
 * Function: main
 * --------------
 * Serves as the entry point of the full application.
 * You'll want to update main to declare several hashsets--
 * one for stop words, another for previously seen urls, etc--
 * and pass them (by address) to BuildIndices and QueryIndices.
 * In fact, you'll need to extend many of the prototypes of the
 * supplied helpers functions to take one or more hashset *s.
 *
 * Think very carefully about how you're going to keep track of
 * all of the stop words, how you're going to keep track of
 * all the previously seen articles, and how you're going to 
 * map words to the collection of news articles where that
 * word appears.
 */

static const char *const kWelcomeTextFile = "welcome.txt";
static const char *const kDefaultFeedsFile = "rss-feeds.txt";
static const char *const kDefaultStopWords = "stop-words.txt";
static const signed long kHashMultiplier = -1664117991L;
static hashset wordsIgnored;
static vector urls;

int main(int argc, char **argv)
{
  HashSetNew(&wordsIgnored, sizeof(char*), 10000, StringHash, cmpStrings, freeString);
  readStopWords(&wordsIgnored);
  VectorNew(&urls, sizeof(Words*), freeWords, 248);
  Welcome(kWelcomeTextFile);
  BuildIndices((argc == 1) ? kDefaultFeedsFile : argv[1]);
  QueryIndices();
  HashSetDispose(&wordsIgnored);
  VectorDispose(&urls);
  return 0;
}

/** 
 * Function: Welcome
 * -----------------
 * Displays the contents of the specified file, which
 * holds the introductory remarks to be printed every time
 * the application launches.  This type of overhead may
 * seem silly, but by placing the text in an external file,
 * we can change the welcome text without forcing a recompilation and
 * build of the application.  It's as if welcomeTextFileName
 * is a configuration file that travels with the application.
 */
 
static const char *const kNewLineDelimiters = "\r\n";
static void Welcome(const char *welcomeTextFileName)
{
  FILE *infile;
  streamtokenizer st;
  char buffer[1024];
  
  infile = fopen(welcomeTextFileName, "r");
  assert(infile != NULL);    
  
  STNew(&st, infile, kNewLineDelimiters, true);
  while (STNextToken(&st, buffer, sizeof(buffer))) {
    printf("%s\n", buffer);
  }
  
  printf("\n");
  STDispose(&st); // remember that STDispose doesn't close the file, since STNew doesn't open one.. 
  fclose(infile);
}

/**
 * Function: BuildIndices
 * ----------------------
 * As far as the user is concerned, BuildIndices needs to read each and every
 * one of the feeds listed in the specied feedsFileName, and for each feed parse
 * content of all referenced articles and store the content in the hashset of indices.
 * Each line of the specified feeds file looks like this:
 *
 *   <feed name>: <URL of remore xml document>
 *
 * Each iteration of the supplied while loop parses and discards the feed name (it's
 * in the file for humans to read, but our aggregator doesn't care what the name is)
 * and then extracts the URL.  It then relies on ProcessFeed to pull the remote
 * document and index its content.
 */

static void BuildIndices(const char *feedsFileName)
{
  FILE *infile;
  streamtokenizer st;
  char remoteFileName[1024];
  
  infile = fopen(feedsFileName, "r");
  assert(infile != NULL);
  STNew(&st, infile, kNewLineDelimiters, true);
  while (STSkipUntil(&st, ":") != EOF) { // ignore everything up to the first selicolon of the line
    STSkipOver(&st, ": ");     // now ignore the semicolon and any whitespace directly after it
    STNextToken(&st, remoteFileName, sizeof(remoteFileName));   
    ProcessFeed(remoteFileName);
  }
  
  STDispose(&st);
  fclose(infile);
  printf("\n");
}


/**
 * Function: ProcessFeed
 * ---------------------
 * ProcessFeed locates the specified RSS document, and if a (possibly redirected) connection to that remote
 * document can be established, then PullAllNewsItems is tapped to actually read the feed.  Check out the
 * documentation of the PullAllNewsItems function for more information, and inspect the documentation
 * for ParseArticle for information about what the different response codes mean.
 */

static void ProcessFeed(const char *remoteDocumentName)
{
  url u;
  urlconnection urlconn;
  
  URLNewAbsolute(&u, remoteDocumentName);
  URLConnectionNew(&urlconn, &u);
  
  switch (urlconn.responseCode) {
      case 0: printf("Unable to connect to \"%s\".  Ignoring...", u.serverName);
              break;
      case 200: PullAllNewsItems(&urlconn);
                break;
      case 301: 
      case 302: ProcessFeed(urlconn.newUrl);
                break;
      default: printf("Connection to \"%s\" was established, but unable to retrieve \"%s\". [response code: %d, response message:\"%s\"]\n",
          u.serverName, u.fileName, urlconn.responseCode, urlconn.responseMessage);
         break;
  };
  
  URLConnectionDispose(&urlconn);
  URLDispose(&u);
}

/**
 * Function: PullAllNewsItems
 * --------------------------
 * Steps though the data of what is assumed to be an RSS feed identifying the names and
 * URLs of online news articles.  Check out "datafiles/sample-rss-feed.txt" for an idea of what an
 * RSS feed from the www.nytimes.com (or anything other server that syndicates is stories).
 *
 * PullAllNewsItems views a typical RSS feed as a sequence of "items", where each item is detailed
 * using a generalization of HTML called XML.  A typical XML fragment for a single news item will certainly
 * adhere to the format of the following example:
 *
 * <item>
 *   <title>At Installation Mass, New Pope Strikes a Tone of Openness</title>
 *   <link>http://www.nytimes.com/2005/04/24/international/worldspecial2/24cnd-pope.html</link>
 *   <description>The Mass, which drew 350,000 spectators, marked an important moment in the transformation of Benedict XVI.</description>
 *   <author>By IAN FISHER and LAURIE GOODSTEIN</author>
 *   <pubDate>Sun, 24 Apr 2005 00:00:00 EDT</pubDate>
 *   <guid isPermaLink="false">http://www.nytimes.com/2005/04/24/international/worldspecial2/24cnd-pope.html</guid>
 * </item>
 *
 * PullAllNewsItems reads and discards all characters up through the opening <item> tag (discarding the <item> tag
 * as well, because once it's read and indentified, it's been pulled,) and then hands the state of the stream to
 * ProcessSingleNewsItem, which handles the job of pulling and analyzing everything up through and including the </item>
 * tag. PullAllNewsItems processes the entire RSS feed and repeatedly advancing to the next <item> tag and then allowing
 * ProcessSingleNewsItem do process everything up until </item>.
 */

static const char *const kTextDelimiters = " \t\n\r\b!@$%^*()_+={[}]|\\'\":;/?.>,<~`";
static void PullAllNewsItems(urlconnection *urlconn)
{
  streamtokenizer st;
  STNew(&st, urlconn->dataStream, kTextDelimiters, false);
  while (GetNextItemTag(&st)) { // if true is returned, then assume that <item ...> has just been read and pulled from the data stream
    ProcessSingleNewsItem(&st);
  }
  
  STDispose(&st);
}

/**
 * Function: GetNextItemTag
 * ------------------------
 * Works more or less like GetNextTag below, but this time
 * we're searching for an <item> tag, since that marks the
 * beginning of a block of HTML that's relevant to us.  
 * 
 * Note that each tag is compared to "<item" and not "<item>".
 * That's because the item tag, though unlikely, could include
 * attributes and perhaps look like any one of these:
 *
 *   <item>
 *   <item rdf:about="Latin America reacts to the Vatican">
 *   <item requiresPassword=true>
 *
 * We're just trying to be as general as possible without
 * going overboard.  (Note that we use strncasecmp so that
 * string comparisons are case-insensitive.  That's the case
 * throughout the entire code base.)
 */

static const char *const kItemTagPrefix = "<item";
static bool GetNextItemTag(streamtokenizer *st)
{
  char htmlTag[1024];
  while (GetNextTag(st, htmlTag, sizeof(htmlTag))) {
    if (strncasecmp(htmlTag, kItemTagPrefix, strlen(kItemTagPrefix)) == 0) {
      return true;
    }
  }  
  return false;
}

/**
 * Function: ProcessSingleNewsItem
 * -------------------------------
 * Code which parses the contents of a single <item> node within an RSS/XML feed.
 * At the moment this function is called, we're to assume that the <item> tag was just
 * read and that the streamtokenizer is currently pointing to everything else, as with:
 *   
 *      <title>Carrie Underwood takes American Idol Crown</title>
 *      <description>Oklahoma farm girl beats out Alabama rocker Bo Bice and 100,000 other contestants to win competition.</description>
 *      <link>http://www.nytimes.com/frontpagenews/2841028302.html</link>
 *   </item>
 *
 * ProcessSingleNewsItem parses everything up through and including the </item>, storing the title, link, and article
 * description in local buffers long enough so that the online new article identified by the link can itself be parsed
 * and indexed.  We don't rely on <title>, <link>, and <description> coming in any particular order.  We do asssume that
 * the link field exists (although we can certainly proceed if the title and article descrption are missing.)  There
 * are often other tags inside an item, but we ignore them.
 */

static const char *const kItemEndTag = "</item>";
static const char *const kTitleTagPrefix = "<title";
static const char *const kDescriptionTagPrefix = "<description";
static const char *const kLinkTagPrefix = "<link";
static void ProcessSingleNewsItem(streamtokenizer *st)
{
  char htmlTag[1024];
  char articleTitle[1024];
  char articleDescription[1024];
  char articleURL[1024];
  articleTitle[0] = articleDescription[0] = articleURL[0] = '\0';
  
  while (GetNextTag(st, htmlTag, sizeof(htmlTag)) && (strcasecmp(htmlTag, kItemEndTag) != 0)) {
    if (strncasecmp(htmlTag, kTitleTagPrefix, strlen(kTitleTagPrefix)) == 0) ExtractElement(st, htmlTag, articleTitle, sizeof(articleTitle));
    if (strncasecmp(htmlTag, kDescriptionTagPrefix, strlen(kDescriptionTagPrefix)) == 0) ExtractElement(st, htmlTag, articleDescription, sizeof(articleDescription));
    if (strncasecmp(htmlTag, kLinkTagPrefix, strlen(kLinkTagPrefix)) == 0) ExtractElement(st, htmlTag, articleURL, sizeof(articleURL));
  }
  
  if (strncmp(articleURL, "", sizeof(articleURL)) == 0) return;     // punt, since it's not going to take us anywhere
  ParseArticle(articleTitle, articleDescription, articleURL);
}

/**
 * Function: ExtractElement
 * ------------------------
 * Potentially pulls text from the stream up through and including the matching end tag.  It assumes that
 * the most recently extracted HTML tag resides in the buffer addressed by htmlTag.  The implementation
 * populates the specified data buffer with all of the text up to but not including the opening '<' of the
 * closing tag, and then skips over all of the closing tag as irrelevant.  Assuming for illustration purposes
 * that htmlTag addresses a buffer containing "<description" followed by other text, these three scanarios are
 * handled:
 *
 *    Normal Situation:     <description>http://some.server.com/someRelativePath.html</description>
 *    Uncommon Situation:   <description></description>
 *    Uncommon Situation:   <description/>
 *
 * In each of the second and third scenarios, the document has omitted the data.  This is not uncommon
 * for the description data to be missing, so we need to cover all three scenarious (I've actually seen all three.)
 * It would be quite unusual for the title and/or link fields to be empty, but this handles those possibilities too.
 */
 
static void ExtractElement(streamtokenizer *st, const char *htmlTag, char dataBuffer[], int bufferLength)
{
  assert(htmlTag[strlen(htmlTag) - 1] == '>');
  if (htmlTag[strlen(htmlTag) - 2] == '/') return;    // e.g. <description/> would state that a description is not being supplied
  STNextTokenUsingDifferentDelimiters(st, dataBuffer, bufferLength, "<");
  RemoveEscapeCharacters(dataBuffer);
  if (dataBuffer[0] == '<') strcpy(dataBuffer, "");  // e.g. <description></description> also means there's no description
  STSkipUntil(st, ">");
  STSkipOver(st, ">");
}

/** 
 * Function: ParseArticle
 * ----------------------
 * Attempts to establish a network connect to the news article identified by the three
 * parameters.  The network connection is either established of not.  The implementation
 * is prepared to handle a subset of possible (but by far the most common) scenarios,
 * and those scenarios are categorized by response code:
 *
 *    0 means that the server in the URL doesn't even exist or couldn't be contacted.
 *    200 means that the document exists and that a connection to that very document has
 *        been established.
 *    301 means that the document has moved to a new location
 *    302 also means that the document has moved to a new location
 *    4xx and 5xx (which are covered by the default case) means that either
 *        we didn't have access to the document (403), the document didn't exist (404),
 *        or that the server failed in some undocumented way (5xx).
 *
 * The are other response codes, but for the time being we're punting on them, since
 * no others appears all that often, and it'd be tedious to be fully exhaustive in our
 * enumeration of all possibilities.
 */

static void ParseArticle(const char *articleTitle, const char *articleDescription, const char *articleURL)
{
  url u;
  urlconnection urlconn;
  streamtokenizer st;

  URLNewAbsolute(&u, articleURL);
  URLConnectionNew(&urlconn, &u);
  
  switch (urlconn.responseCode) {
      case 0: printf("Unable to connect to \"%s\".  Domain name or IP address is nonexistent.\n", articleURL);
        break;
      case 200: printf("[%s] Indexing \"%s\"\n", u.serverName, articleTitle);
          STNew(&st, urlconn.dataStream, kTextDelimiters, false);
    ScanArticle(&st, articleTitle, articleDescription, articleURL);
    STDispose(&st);
    break;
      case 301:
      case 302: // just pretend we have the redirected URL all along, though index using the new URL and not the old one...
                ParseArticle(articleTitle, articleDescription, urlconn.newUrl);
    break;
      default: printf("Unable to pull \"%s\" from \"%s\". [Response code: %d] Punting...\n", articleTitle, u.serverName, urlconn.responseCode);
         break;
  }
  
  URLConnectionDispose(&urlconn);
  URLDispose(&u);
}

/**
 * Function: ScanArticle
 * ---------------------
 * Parses the specified article, skipping over all HTML tags, and counts the numbers
 * of well-formed words that could potentially serve as keys in the set of indices.
 * Once the full article has been scanned, the number of well-formed words is
 * printed, and the longest well-formed word we encountered along the way
 * is printed as well.
 *
 * This is really a placeholder implementation for what will ultimately be
 * code that indexes the specified content.
 */

//MODIFY
//gcc -m32 -o rss-news-search rss-news-search.c librssnews.a 
//./rss-news-search rss-tech.txt
static void ScanArticle(streamtokenizer *st, const char *articleTitle, const char *unused, const char *articleURL)
{
  int numWords = 0;
  char word[1024];
  char longestWord[1024] = {'\0'};
  char *url = (char*)calloc(strlen(articleTitle) + 2, sizeof(char));
  
  memmove(url, articleTitle, strlen(articleTitle) * sizeof(char));
  strcat(url, " ");
  Words *url2 = (Words*)malloc(sizeof(Words));
  url2->url = (char*)calloc(strlen(url), sizeof(char));
  memmove(url2->url, url, strlen(url) * sizeof(char));
  free(url);
  url2->words = (hashset*)malloc(sizeof(hashset));
  HashSetNew(url2->words, sizeof(Freq*), 1000, hashFrec, cmpFrec, freeFrec);

  while (STNextToken(st, word, sizeof(word))) {
    if (strcasecmp(word, "<") == 0) {
      SkipIrrelevantContent(st);
    } else {
      RemoveEscapeCharacters(word);
      if (WordIsWellFormed(word)) {
        char *current = (char*)calloc(strlen(word) + 1, sizeof(char));
        memmove(current, word, strlen(word) * sizeof(char));
        char **res = (char**)HashSetLookup(&wordsIgnored, &current);
        if(res == NULL){
          Freq *frecuency = (Freq*)malloc(sizeof(Freq));
          frecuency->word = (char*)calloc(strlen(current) + 1, sizeof(char));
          memmove(frecuency->word, current, strlen(current) * sizeof(char));
          frecuency->freq = 1;
          free(current);
          Freq **included = (Freq**)HashSetLookup(url2->words,&frecuency);
          if(included == NULL){
            HashSetEnter(url2->words, &frecuency);
          } else{
            frecuency->freq += (*included)->freq;
            HashSetEnter(url2->words, &frecuency);
          }
        }
        numWords++;
        if (strlen(word) > strlen(longestWord)){
          strcpy(longestWord, word);
        }
      }
    }
  }
  VectorAppend(&urls, &url2);
}

/** 
 * Function: QueryIndices
 * ----------------------
 * Standard query loop that allows the user to specify a single search term, and
 * then proceeds (via ProcessResponse) to list up to 10 articles (sorted by relevance)
 * that contain that word.
 */

static void QueryIndices()
{
  char response[1024];
  while (true) {
    printf("Please enter a single query term that might be in our set of indices [enter to quit]: ");
    fgets(response, sizeof(response), stdin);
    response[strlen(response) - 1] = '\0';
    if (strcasecmp(response, "") == 0) break;
    ProcessResponse(response);
  }
}

/** 
 * Function: ProcessResponse
 * -------------------------
 * Placeholder implementation for what will become the search of a set of indices
 * for a list of web documents containing the specified word.
 */

//MODIFY
static void ProcessResponse(const char *word)
{
  char **wordIgnored = (char**)HashSetLookup(&wordsIgnored, &word);
  int found = 0;
  if(wordIgnored == NULL){
    int i;
    Freq *tempFrec = (Freq*)malloc(sizeof(Freq));
    tempFrec->word=(char*)calloc(strlen(word) + 1, sizeof(char));
    memmove(tempFrec->word, word, sizeof(char) * strlen(word));
    tempFrec->freq = 0;
    Words **urlget;
    Freq **r;
    for(i = 0; i < VectorLength(&urls); i++){
      urlget = (Words**)VectorNth(&urls, i);
      r = (Freq**)HashSetLookup((*urlget)->words, &tempFrec);
      if(r != NULL){
        printf("\"%s\"[search term occurs %d times]\n", (*urlget)->url, (*r)->freq);
        found = 1;
      }
    }
    free(tempFrec->word);
    free(tempFrec);
  }
  else{
    printf("\"%s\" is too common a word to be taken seriously. Please be more specific.\n", word);
  }
  if(found == 0){
     printf("None of today's news articles contain the word \"%s\".\n", word);
  }
}

/**
 * Predicate Function: WordIsWellFormed
 * ------------------------------------
 * Before we allow a word to be inserted into our map
 * of indices, we'd like to confirm that it's a good search term.
 * One could generalize this function to allow different criteria, but
 * this version hard codes the requirement that a word begin with 
 * a letter of the alphabet and that all letters are either letters, numbers,
 * or the '-' character.  
 */

static bool WordIsWellFormed(const char *word)
{
  int i;
  if (strlen(word) == 0) return true;
  if (!isalpha((int) word[0])) return false;
  for (i = 1; i < strlen(word); i++)
    if (!isalnum((int) word[i]) && (word[i] != '-')) return false; 

  return true;
}

static void readStopWords(hashset *h){
  FILE *f = fopen(kDefaultStopWords, "r");
  assert(f != NULL && "cannot open the file");
  char *word = (char*)calloc(31, sizeof(char));
  char chr = fgetc(f);
  int i = 0;
  while(chr != EOF){
    if(chr != '\n'){
      word[i] = chr;
      i++;
    } else{
      HashSetEnter(h, &word);
      word = (char*)calloc(21, sizeof(char));
      i = 0;
    }
    chr = fgetc(f);
  }
  HashSetEnter(h, &word);
  fclose(f);
}

static int StringHash(const void *a, int numBuckets)  
{            
  char **str = (char**)a;
  int i;
  unsigned long hashcode = 0;
  for (i = 0; i < strlen(*str); i++)  
    hashcode = hashcode * kHashMultiplier + tolower((*str)[i]);  
  return hashcode % numBuckets;                                
}

static int hashFrec(const void *a, int numberBuckets){
  Freq **frec = (Freq**)a;
  return StringHash(&((*frec)->word), numberBuckets);
}

static int cmpStrings(const void *a, const void *b){
  char **string1 = (char**)a;
  char **string2 = (char**)b;
  return strcmp(*string1, *string2);
}

static int cmpFrec(const void *a, const void *b){
  Freq **f1 = (Freq**)a;
  Freq **f2 = (Freq**)b;
  return cmpStrings(&((*f1)->word), &((*f2)->word));
}

static void freeString(void *a){
  char **string = (char**)a;
  free(*string);
}

static void freeFrec(void *a){
  Freq **frec = (Freq**)a;
  free((*frec)->word);
  free(*frec);
}

static void freeWords(void *a){
  Words **wr = (Words**)a;
  free((*wr)->url);
  HashSetDispose((*wr)->words);
  free((*wr)->words);
  free(*wr);
}
