typedef struct{
  char *word;
  int freq;
} Freq;

typedef struct{
  char *url;
  hashset *words;
} Words;

static void Welcome(const char *welcomeTextFileName);
static void BuildIndices(const char *feedsFileName);
static void ProcessFeed(const char *remoteDocumentName);
static void PullAllNewsItems(urlconnection *urlconn);
static bool GetNextItemTag(streamtokenizer *st);
static void ProcessSingleNewsItem(streamtokenizer *st);
static void ExtractElement(streamtokenizer *st, const char *htmlTag, char dataBuffer[], int bufferLength);
static void ParseArticle(const char *articleTitle, const char *articleDescription, const char *articleURL);
static void ScanArticle(streamtokenizer *st, const char *articleTitle, const char *unused, const char *articleURL);
static void QueryIndices();
static void ProcessResponse(const char *word);
static bool WordIsWellFormed(const char *word);
static void readStopWords(hashset *h);
static int StringHash(const void *a, int numBuckets); 
static int hashFrec(const void *a, int numberBuckets);
static int cmpStrings(const void *a, const void *b);
static int cmpFrec(const void *a, const void *b);
static void freeString(void *a);
static void freeFrec(void *a);
static void freeWords(void *a);