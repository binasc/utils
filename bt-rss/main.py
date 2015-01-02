#!/usr/bin/env python
#
# Copyright 2007 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
from google.appengine.api import urlfetch
from google.appengine.ext import db
import webapp2
import feedparser
from urlparse import urlparse
from lxml import etree
import PyRSS2Gen
import logging
import time
import datetime
import cPickle
import urllib

def redirectMydrivers(url, html):
    postpage = html.xpath('//div[@class="postpage"]')
    if len(postpage) > 0:
        links = postpage[0].xpath('.//a[@href][last()]')
        if len(links) > 0:
            href = catenateUrl(url, links[0].attrib["href"])
            return href
    return None

methods = {
    'news.mydrivers.com':
        ('//div[@class="news_info"]',
        (
            './/p[@class="news_bq"][1]',
            './/table[last()]'
        ),
        redirectMydrivers)
}

class UrlContent(db.Model):
    ts = db.DateTimeProperty(auto_now_add=True)
    url = db.LinkProperty()
    content = db.BlobProperty()

class TestHandler(webapp2.RequestHandler):
    def get(self):
        while True:
            values = db.GqlQuery("SELECT __key__ FROM UrlContent ORDER BY __key__ DESC")
            count = values.count();
            db.delete(values)
            if count < 1000:
                break
        
        link = "http://news.mydrivers.com/1/269/269246.htm"
        page = fetchWebContent(link)
        (content, redirect) = getDescription(link, page);
        
        self.response.write(content)

def fetchWebContent(url):
    start = time.time();
    try:
        result = urlfetch.fetch(url)
    except urlfetch.Error as e:
        logging.error("urlfetch failed %s ms %s", str((time.time() - start) * 1000), e)
        return None;
    else:
        logging.debug("urlfetch cost %s ms %s", str((time.time() - start)*1000), url)

        if result.status_code != 200:
            logging.error("status code: %d %s", result.status_code, url)
            return None;
        else:
            logging.debug("status code: %d %s", result.status_code, url)
            return result.content

class asycFetch():
    rpcs = []
    def fetch(url, retries=0, deadline=None, callback=None):
        def create_callback_wrapper(rpc, retry, callback):
            def callback_wrapper():
                try:
                    response = rpc.get_result()
                except Exception as e:
                    logging.error("%s exception raised", url)
                    if retry > 0:
                        asycFetch.fetch(url, retry - 1, deadline, callback)
                else:
                    if response.status_code != 200:
                        logging.error("%s http status code: %d", url, response.status_code)
                        if retry > 0:
                            asycFetch.fetch(url, retry - 1, deadline, callback)
                    else:
                        callback(response)
            return lambda: callback_wrapper()

        rpc = urlfetch.create_rpc(deadline)
        rpc.callback = create_callback_wrapper(rpc, retries, callback)
        asycFetch.rpcs.append(rpc)
        urlfetch.make_fetch_call(rpc, url)

    def wait():
        for rpc in asycFetch.rpcs:
            rpc.wait()
        asycFetch.rpcs = []

    fetch = staticmethod(fetch)
    wait = staticmethod(wait)

class RssHandler(webapp2.RequestHandler):
    def create_callback(self, entry, parser, items, n, redirected=False):
        def callback(response):
            (content, redirect) = parser(entry.link, response.content)
            if content == None:
                logging.error("%s parse error", entry.link)
                return
            elif redirect != None and redirected == False:
                asycFetch.fetch(redirect, 2, 5, self.create_callback(entry, parser, items, n, True))
                return

            items[n] = PyRSS2Gen.RSSItem(
                title = entry.title,
                link = entry.link,
                author = entry.author,
                guid = entry.guid,
                categories = [entry.category],
                pubDate = entry.published,
                comments = entry.comments,
                description = content);
        return lambda response: callback(response)

    def get(self):
        begin = time.time()

        rss = self.request.get('url')
        if rss == None:
            self.response.status = 400
            return
        rss = urllib.unquote(rss)

        urlfetch.set_default_fetch_deadline(5)
        xml = fetchWebContent(rss)
        if xml == None:
            self.response.status = 503
            return

        feed = feedparser.parse(xml)
        items = [None] * len(feed.entries)

        n = 0;
        for entry in feed.entries:
            asycFetch.fetch(entry.link, 2, 5, self.create_callback(entry, getDescription, items, n))
            n += 1

        asycFetch.wait()
        items = filter(lambda item: item != None, items)

        logging.debug("fetch costed %s", str(time.time() - begin))

        rss = PyRSS2Gen.RSS2(
            title = feed.channel.title,
            link = feed.channel.link,
            description = feed.channel.description,
            image = PyRSS2Gen.Image(
                feed.channel.image.url,
                feed.channel.image.title,
                feed.channel.image.link),
            language = feed.channel.language,
            generator = feed.channel.generator,
            copyright = feed.channel.copyright,
            ttl = feed.channel.ttl,
            pubDate = feed.channel.published,
            categories = [feed.channel.category],
            items = items);

        self.response.headers['Content-Type'] = 'text/xml; charset=utf-8'
        self.response.write(rss.to_xml("utf-8"))

def catenateUrl(url, path):
    if path[0] == '/':
        return url.scheme + '://' + url.netloc + path
    else:
        return url.scheme + '://' + url.netloc + url.path[0: url.path.rfind('/') + 1] + path

def alterLink(url, attr, elements):
    for e in elements:
        c = e.attrib.get(attr)
        if c != None and c.find('://') == -1:
            e.attrib[attr] = catenateUrl(url, c)

def getDescription(link, page):
    page = page.decode('gbk', 'replace')

    url = urlparse(link)
    method = methods[url.netloc]
    if method == None:
        return (None, None)

    html = etree.HTML(page); 
    content = html.xpath(method[0])
    if len(content) == 0:
        return (None, None)

    for rmpath in method[1]:
        for toremove in content[0].xpath(rmpath):
            toremove.getparent().remove(toremove)

    alterLink(url, 'href', content[0].xpath('.//a[@href]'))
    alterLink(url, 'src', content[0].xpath('.//img[@src]'))

    href = method[2](url, html)
    if href != None:
        logging.debug("redirect to %s", href);

    return (etree.tostring(content[0], method='html', encoding='utf-8'), href)

app = webapp2.WSGIApplication([
    ('/rss.xml', RssHandler),
    ('/test', TestHandler)
], debug=True)
