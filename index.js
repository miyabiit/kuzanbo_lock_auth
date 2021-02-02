'use strict';

require('dotenv').config();
const fs = require("fs");
const puppeteer = require('puppeteer');
const moment = require('moment');

process.env.NODE_ENV = process.env.NODE_ENV || 'development';
//var express = require('express');
//var cors = require('cors');
const fetch = require('node-fetch');
var Obniz = require("obniz");
const { URLSearchParams } = require('url');

console.log(`device id = ${process.env.OBNIZ_DEVICE_ID}`)
var obniz = new Obniz(process.env.OBNIZ_DEVICE_ID, {reset_obniz_on_ws_disconnection: false});

let passwords = [];

//// TODO: API認証
//// ref: https://staging.minpakuin.jp/api/doc/v1/request/swagger
//// GET /reserves?canceled=false&checkin_from=${today}&checkin_to=${today}
//function fetchPasswordsByAPI() {
//  const userId = process.env.PASSWORD_SERVER_USER_ID;
//  const password = process.env.PASSWORD_SERVER_PASSWORD;
//  fetch(url,
//    {
//      method: 'GET',
//      headers: {
//        Authorization: "Basic " + new Buffer(`${userId}:${password}`).toString("base64"),
//      }
//    }
//  )
//    .then((res) => console.log(res))
//    .catch((err) => console.error(err));
//}

// (login) GET https://staging.minpakuin.jp/host/login
// (reserve page) GET https://staging.minpakuin.jp/host/reserves?canceled=false&checkin_from=2020-10-27&checkin_to=2020-10-27&order=checkin_asc
function fetchPasswordsByPage() {
  (async () => {
    const browser = await puppeteer.launch();
    const page = await browser.newPage();
    await page.goto(process.env.PASSWORD_SERVER_URL + 'login');
    await page.type('input[name="login_id"]', process.env.PASSWORD_SERVER_USER_ID);
    await page.type('input[name="password"]', process.env.PASSWORD_SERVER_PASSWORD);
    page.click('button[type="submit"]');
    await page.waitForNavigation({ waitUntil: 'domcontentloaded' });

    const apiUrl = process.env.PASSWORD_SERVER_URL + 'reserve-api/list'
    await page.setRequestInterception(true);
    let targetDate = moment().format("YYYY-MM-DD");
    let pageNumber = 1;

    const fetchPasswords = async (targetDate, pageNumber) => {
      let pageCount = null;
      page.removeAllListeners('request');
      page.on('request', request => {
        //console.log(`${request.url()} = ${apiUrl}`);
        const overrides = {};
        if (request.url() === apiUrl) {
          overrides.method = 'POST';
          overrides.headers = {
            'Content-Type': 'application/json; charset=UTF-8',
          };
          // checkout_from パラメータがない
          overrides.postData = `{"checkin_to": "${targetDate}", "order": "checkout_desc", "page": ${pageNumber}}`;
        }
        request.continue(overrides);
      });

      await page.goto(apiUrl);

      let content = await page.content(); 
      let json = await page.evaluate(() =>  {
          return JSON.parse(document.querySelector("body").innerText); 
      }); 
      //console.log('fetch by api')
      //console.log(json);
      let passwords = [];
      pageCount = Math.ceil(parseInt(json.page_set.total) / parseInt(json.page_set.show_once));
      let isEnd = false;
      if (json.reserves) {
        for (const reserve of json.reserves) {
          if (reserve.rooms) {
            for (const room of reserve.rooms) {
              if (room.key_no) {
                const matched = room.key_no.match(/\d+/);
                if (matched) {
                  const pswd = matched[0];
                  if (passwords.indexOf(pswd) === -1) {
                    passwords.push(pswd);
                  }
                }
              }
            }
          }
          if (!isEnd && reserve.checkout) {
            const checkout = reserve.checkout.toString();
            let checkoutDate = moment(checkout, 'YYYY/MM/DD');
            if (moment(targetDate).isAfter(checkoutDate)) {
              isEnd = true;
            }
          }
        }
      }
      return {passwords: passwords, isEnd: isEnd, pageCount: pageCount};
    };

    const firstResult = await fetchPasswords(targetDate, pageNumber);
    let fetchedPasswords = firstResult.passwords;
    const pageCount = firstResult.pageCount;
    for (pageNumber = 2; pageNumber <= pageCount; ++pageNumber) {
      const result = await fetchPasswords(targetDate, pageNumber)
      const resultPasswords = result.passwords;
      fetchedPasswords = fetchedPasswords.concat(resultPasswords);
      if (result.isEnd) {
        break;
      }
    }

    passwords = fetchedPasswords;

    console.log('fetched passwords = ')
    console.log(passwords)

    await browser.close();
  })();
}

fetchPasswordsByPage();

function convertStrToDataBytes(str) {
  let bytes = [];
  for (let i = 0; i < str.length; ++i) {
    bytes.push(str.charCodeAt(i));
  }
  return bytes;
}

function parseCommand(line) {
  if (line.startsWith('update on')) {
    obniz.plugin.send(["U".charCodeAt(0)]);
    console.log('version-up response');
  } else if (line.startsWith('set ')) {
    const words = line.split(' ');
    const paramName = words[1];
    if (paramName === 'SleepByNoActionMin') {
      const value = parseInt(words[2]) * 60 * 1000;
      const command = "$set:sleep_timeout=" + value;
      obniz.plugin.send(convertStrToDataBytes(command));
      console.log(command);
    } else if (paramName === 'WakeupIntervalMin') {
      const value = parseInt(words[2]) * 60 * 1000;
      const command = "$set:wakeup_timeout=" + value;
      obniz.plugin.send(convertStrToDataBytes(command));
      console.log(command);
    }
  }
}

console.log('server start')
obniz.onconnect = async function() {
  obniz.plugin.onreceive = (data) => {
    //console.log(data);
    console.log(`onReceive, data = ${data}`);
    if (data.length > 0 && data[0] == '$'.charCodeAt(0)) { // command
      if (data.length == 5 && data[1] == 'u'.charCodeAt(0) && data[2] == 'p'.charCodeAt(0) && data[3] == 'd'.charCodeAt(0) && data[4] == '?'.charCodeAt(0)) { // receive version-up confirmation
        fs.readFile('command.txt', 'utf-8', (err, cmd_txt) => {
          if (err) {
            console.log('command.txt is not found');
          } else {
            for (let line of cmd_txt.split("\n")) {
              parseCommand(line);
            }
          }
        });
      } else if (data.length == 2 && data[1] == 'f'.charCodeAt(0)) { // fetch command
        fetchPasswordsByPage();
      } else if (data.length == 3 && data[1] == 'a'.charCodeAt(0) && data[2] == '?'.charCodeAt(0)) { // ack
        obniz.plugin.send(["A".charCodeAt(0)]);
        console.log('ACK!');
      }
    } else {
      for (const password of passwords) {
        let matched = true;
        if (password.length != data.length) {
          continue;
        }
        for (let i = 0; i < data.length; ++i) {
          if (data[i] != password.charCodeAt(i)) {
            matched = false;
            break;
          }
        }
        if (matched) {
          console.log('MATCH')
          obniz.plugin.send(["O".charCodeAt(0)]);
          return;
        }
      }
      console.log('UNMATCH')
      obniz.plugin.send(["N".charCodeAt(0)]);            
    }
  };
};
