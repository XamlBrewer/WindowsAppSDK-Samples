﻿// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.

#include <iostream>

#include <wil/result.h>
#include <wil/cppwinrt.h>

#include <winrt/Windows.ApplicationModel.Background.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.Windows.AppLifecycle.h>
#include <winrt/Microsoft.Windows.PushNotifications.h>
#include <winrt/Windows.Globalization.DateTimeFormatting.h>

using namespace winrt::Microsoft::Windows::AppLifecycle;
using namespace winrt::Microsoft::Windows::PushNotifications;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Globalization::DateTimeFormatting;

// To obtain an AAD RemoteIdentifier for your app,
// follow the instructions on https://docs.microsoft.com/en-us/windows/apps/windows-app-sdk/notifications/push/push-quickstart#configure-your-apps-identity-in-azure-active-directory
winrt::guid remoteId{ "00000000-0000-0000-0000-000000000000"}; // Replace this with your own RemoteId

winrt::Windows::Foundation::IAsyncOperation<PushNotificationChannel> RequestChannelAsync()
{
    auto channelOperation = PushNotificationManager::CreateChannelAsync(remoteId);

    // Setup the inprogress event handler
    channelOperation.Progress(
        [](auto&& sender, auto&& args)
        {
            if (args.status == PushNotificationChannelStatus::InProgress)
            {
                // This is basically a noop since it isn't really an error state
                std::cout << "\nWNS ChannelURI request is in progress." << std::endl;
            }
            else if (args.status == PushNotificationChannelStatus::InProgressRetry)
            {
                LOG_HR_MSG(
                    args.extendedError,
                    "The WNS ChannelURI request is in back-off retry mode because of a retryable error! Expect delays in acquiring it. RetryCount = %d",
                    args.retryCount);
            }
        });

    auto result = co_await channelOperation;

    if (result.Status() == PushNotificationChannelStatus::CompletedSuccess)
    {
        auto channel = result.Channel();

        DateTimeFormatter formater = DateTimeFormatter(L"on {month.abbreviated} {day.integer(1)}, {year.full} at {hour.integer(1)}:{minute.integer(2)}:{second.integer(2)}");

        std::cout << "\nWNS ChannelURI: " << winrt::to_string(channel.Uri().ToString()) << std::endl;
        std::wcout << L"\nThe WNS ChannelURI will expire " << formater.Format(channel.ExpirationTime()).c_str() << std::endl;

        // It's the caller's responsibility to keep the channel alive
        co_return channel;
    }
    else if (result.Status() == PushNotificationChannelStatus::CompletedFailure)
    {
        LOG_HR_MSG(result.ExtendedError(), "We hit a critical non-retryable error with the WNS ChannelURI request!");
        co_return nullptr;
    }
    else
    {
        LOG_HR_MSG(result.ExtendedError(), "Some other failure occurred.");
        co_return nullptr;
    }

};

winrt::Microsoft::Windows::PushNotifications::PushNotificationChannel RequestChannel()
{
    auto task = RequestChannelAsync();
    if (task.wait_for(std::chrono::seconds(300)) != AsyncStatus::Completed)
    {
        task.Cancel();
        return nullptr;
    }

    auto result = task.GetResults();
    return result;
}

// Subscribe to an event which will get signaled whenever a foreground notification arrives.
void SubscribeForegroundEventHandler(const winrt::Microsoft::Windows::PushNotifications::PushNotificationChannel& channel)
{
    winrt::event_token token = channel.PushReceived([](auto const&, PushNotificationReceivedEventArgs const& args)
        {
            auto payload = args.Payload();

            // Do stuff to process the raw payload
            std::string payloadString(payload.begin(), payload.end());
            std::cout << "\nPush notification content received from FOREGROUND: " << payloadString << std::endl;

            // Set handled to true to prevent the same notification to pop up through background activation
            args.Handled(true);
        });
}

int main()
{
    if (PushNotificationManager::IsActivatorSupported(PushNotificationRegistrationActivators::PushTrigger | PushNotificationRegistrationActivators::ComActivator))
    {
        PushNotificationActivationInfo info(
            PushNotificationRegistrationActivators::PushTrigger | PushNotificationRegistrationActivators::ComActivator,
            winrt::guid("ccd2ae3f-764f-4ae3-be45-9804761b28b2")); // same clsid as app manifest

        PushNotificationManager::RegisterActivator(info);
	}

    auto args = AppInstance::GetCurrent().GetActivatedEventArgs();
    auto kind = args.Kind();
    switch (kind)
    {
        // When it is launched normally (by the users, or from the debugger), the sample requests a WNS ChannelURI and
        // displays it, then waits for notifications. This user can take a copy of the WNS ChannelURI and use it to send
        // notifications to the sample
        case ExtendedActivationKind::Launch:
        {
            // Request a WNS ChannelURI which can be passed off to an external app to send notifications to.
            // The WNS ChannelURI uniquely identifies, this app for this user and device.
            PushNotificationChannel channel = RequestChannel();

            // Setup an event handler, so we can receive notifications in the foreground while the app is running.
            if (channel)
            {
                SubscribeForegroundEventHandler(channel);
            }
            else
            {
                std::cout << "\nThere was an error obtaining the WNS ChannelURI" << std::endl;
            }

            std::cout << "\nPress 'Enter' at any time to exit App." << std::endl;
            std::cin.ignore();
        }
        break;

        // When it is activated from a push notification, the sample only displays the notification.
        // It doesn’t register for foreground activation of perform any other actions
        // because background activation is meant to let app perform only small tasks in order to preserve battery life.
        case ExtendedActivationKind::Push:
        {
            PushNotificationReceivedEventArgs pushArgs = args.Data().as<PushNotificationReceivedEventArgs>();

            // Call GetDeferral to ensure that code runs in low power
            auto deferral = pushArgs.GetDeferral();

            auto payload = pushArgs.Payload();

            // Do stuff to process the raw notification payload
            std::string payloadString(payload.begin(), payload.end());
            std::cout << "\nPush notification content received from BACKGROUND: " << payloadString.c_str() << std::endl;
            std::cout << "\nPress 'Enter' to exit the App." << std::endl;

            // Call Complete on the deferral when finished processing the payload.
            // This removes the override that kept the app running even when the system was in a low power mode.
            deferral.Complete();
            std::cin.ignore();
        }
        break;

        default:
            // Unexpected activation type
            std::cout << "\nUnexpected activation type" << std::endl;
            std::cout << "\nPress 'Enter' to exit the App." << std::endl;
            std::cin.ignore();
            break;
    }

   if (PushNotificationManager::IsActivatorSupported(PushNotificationRegistrationActivators::ComActivator))
    {
        // Don't unregister PushTrigger because we still want to receive push notifications from background infrastructure.
        PushNotificationManager::UnregisterActivator(PushNotificationRegistrationActivators::ComActivator);
    }
}
