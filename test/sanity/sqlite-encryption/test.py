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
    
#2nd run, try to reopen the database, should be success
    driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
    driver.implicitly_wait(10)
    button = driver.find_element_by_id('Read')
    button.click() # click the button
    result = driver.find_element_by_id('result').get_attribute('innerHTML')
    print "test 2, reading the encrypted database"
    print result
    assert("success" in result)
    driver.close()
    driver.quit()

#3rd run, rekey
    driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
    driver.implicitly_wait(10)
    button = driver.find_element_by_id('Rekey')
    button.click() # click the button
    result = driver.find_element_by_id('result').get_attribute('innerHTML')
    print "test 3, rekey the database"
    print result
    assert("success" in result)
    driver.close()
    driver.quit()

#4th run, read the rekey
    driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
    driver.implicitly_wait(10)
    button = driver.find_element_by_id('ReadRekey')
    button.click() # click the button
    result = driver.find_element_by_id('result').get_attribute('innerHTML')
    print "test 4, read database with new key"
    print result
    assert("success" in result)
    driver.close()
    driver.quit()

#5th run, try to open with wrong key, should fail
    driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
    driver.implicitly_wait(10)
    button = driver.find_element_by_id('WrongKey')
    button.click() # click the button
    result = driver.find_element_by_id('result').get_attribute('innerHTML')
    print "test 5, openning the database with wrong key, should fail"
    print result
    assert(result.find('file is encrypted') != -1)

finally:
    driver.close()
    driver.quit()
    shutil.rmtree(user_data_dir)
