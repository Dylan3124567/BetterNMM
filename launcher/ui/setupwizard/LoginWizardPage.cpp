#include "LoginWizardPage.h"
#include "nexus/NexusAccount.h"
#include "ui/dialogs/NexusLoginDialog.h"
#include "ui_LoginWizardPage.h"

#include "Application.h"

LoginWizardPage::LoginWizardPage(QWidget* parent) : BaseWizardPage(parent), ui(new Ui::LoginWizardPage)
{
    ui->setupUi(this);
}

LoginWizardPage::~LoginWizardPage()
{
    delete ui;
}

void LoginWizardPage::initializePage() {}

bool LoginWizardPage::validatePage()
{
    return true;
}

void LoginWizardPage::retranslate()
{
    ui->retranslateUi(this);
}

void LoginWizardPage::on_pushButton_clicked()
{
    auto account = NexusLoginDialog::ask(this);
    if (account) {
        APPLICATION->accounts()->upsert(*account);
        if (wizard()->currentId() == wizard()->pageIds().last()) {
            wizard()->accept();
        } else {
            wizard()->next();
        }
    }
}
