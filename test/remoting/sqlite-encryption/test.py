import time
import os
import shutil

from selenium import webdriver
from selenium.webdriver.chrome.options import Options
chrome_options = Options()
testdir = os.path.dirname(os.path.abspath(__file__))
chrome_options.add_argument("nwapp=" + testdir)
user_data_dir = os.path.join(testdir, 'userdata')
chrome_options.add_argument("user-data-dir=" + user_data_dir)
try:
    shutil.rmtree(user_data_dir)
except:
    pass

driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
try:
  
#1st run, create the database
    driver.implicitly_wait(10)
    print driver.current_url
    button = driver.find_element_by_id('Create')
    button.click() # click the button
    result = driver.find_element_by_id('result').get_attribute('innerHTML')
    print "test 1, creating and reading the database"
    print result
    assert("success" in result)
    driver.close()
    driver.quit()

#2nd run, try to open with wrong key, should fail
    driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
    driver.implicitly_wait(10)
    button = driver.find_element_by_id('WrongKey')
    button.click() # click the button
    result = driver.find_element_by_id('result').get_attribute('innerHTML')
    print "test 2, openning the database with wrong key, should fail"
    print result
    assert(result.find('file is encrypted') != -1)
    driver.close()
    driver.quit()

#3rd run, try to reopen the database, should be success
    driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
    driver.implicitly_wait(10)
    button = driver.find_element_by_id('Read')
    button.click() # click the button
    result = driver.find_element_by_id('result').get_attribute('innerHTML')
    print "test 3, reading the encrypted database"
    print result
    assert("success" in result)
finally:
    driver.close()
    driver.quit()
    shutil.rmtree(user_data_dir)
