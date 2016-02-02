import time
import os
import pyautogui

from selenium import webdriver
from selenium.webdriver.chrome.options import Options

chrome_options = Options()
chrome_options.add_argument("--disable-features=nw2")
chrome_options.add_argument("nwapp=" + os.path.dirname(os.path.abspath(__file__)))

driver = webdriver.Chrome(executable_path=os.environ['CHROMEDRIVER'], chrome_options=chrome_options)
try:
    driver.implicitly_wait(15)
    print driver.current_url
    button = driver.find_element_by_id('left-box')
    print button.text
    button.click() # click the button

    print "test 1 drag test"
    init_x, init_y, init_width, init_height = driver.execute_script("return winMetrics;")
    print "x:%d, y:%d, width:%d, height:%d" % (init_x, init_y, init_width, init_height)
    titlebar_height = 28
    pyautogui.moveTo(init_x + 16, init_y + init_height - titlebar_height * 0.5)
    pyautogui.dragRel(100, 100, 1, button='left')
    if os.name == 'nt':
        init_x -= 5; init_y -= 5

    driver.execute_script("getMetric();")
    x, y, width, height = driver.execute_script("return winMetrics;")
    print "x:%d, y:%d, width:%d, height:%d" % (x, y, width, height)
    assert init_x + 100 == x and init_y + 100 == y and init_width == width and init_height == height, "window should be moved by 100,100"

    print "test 2 double click test"
    pyautogui.doubleClick()
    driver.execute_script("getMetric();")
    x, y, width, height = driver.execute_script("return winMetrics;")
    print "x:%d, y:%d, width:%d, height:%d" % (x, y, width, height)
    assert init_x + 100 == x and init_y + 100 == y and init_width == width and init_height == height, "window's size shouldn't change after double click"

    print "test 3 title bar double click test"
    pyautogui.moveTo(x + width * 0.5, y + titlebar_height * 0.5)
    pyautogui.doubleClick()
    result = driver.find_element_by_id('result maximize 1')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

    print "test 4 left titlebar close button"
    driver.execute_script("getMetric();")
    x, y, width, height = driver.execute_script("return winMetrics;")
    pyautogui.moveTo(x + 16, y + titlebar_height + 11 + 17 * 0.5)
    pyautogui.click()
    result = driver.find_element_by_id('result left-titlebar-close-button')
    print result.get_attribute('innerHTML')
    assert("success" in result.get_attribute('innerHTML'))

finally:
  driver.quit()
